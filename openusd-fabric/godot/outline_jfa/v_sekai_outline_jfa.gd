# Copyright 2026 The openusd-fabric authors.
# SPDX-License-Identifier: MIT
#
# V-Sekai JFA outline CompositorEffect (CHI-255).
#
# Attach to a Camera3D's Compositor.compositor_effects to draw the JFA
# outline on top of the resolved scene colour. Reads a per-frame
# silhouette ID/width AOV produced by a material-side shader (the
# VSekaiMToonAPI fragment writes its material_id + outlineWidthFactor
# into that target during the main pass) and runs:
#
#   silhouette_init.glsl   -> seeds the JFA ping-pong texture
#   jfa_step.glsl          -> ceil(log2(max_outline_width_px)) passes
#   final.glsl             -> distance threshold + outline composite
#
# Per-material outline colours/widths live in `palette_buffer`, kept in
# sync by the idtx-flow USD import step from the schema attributes
# under v_sekai:mtoon:outline*.
#
# Wired up at runtime, not in the scene tree, so multiple cameras can
# share the same CompositorEffect instance without resource conflicts.

class_name VSekaiOutlineJFA
extends CompositorEffect

# GLSL kernels are emitted by `pixi run -e slang emit-shader-glsl` from
# the .slang sources under shaders/outline_jfa/. Project packaging copies
# the resulting build/shaders/outline_jfa/ into this res:// path.
const SHADER_DIR := "res://addons/openusd_fabric/outline_jfa/"
const MAX_OUTLINE_WIDTH_PX := 64
const LOCAL_SIZE := 8

@export var depth_occluded: bool = true
@export var depth_bias_metres: float = 0.0001
@export var max_outline_width_px: int = MAX_OUTLINE_WIDTH_PX

var _rd: RenderingDevice
var _shader_init: RID
var _shader_step: RID
var _shader_final: RID
var _pipeline_init: RID
var _pipeline_step: RID
var _pipeline_final: RID

# Ping-pong seed textures, allocated lazily once the AOV size is known.
var _seed_a: RID
var _seed_b: RID
var _seed_size: Vector2i = Vector2i.ZERO


func _init() -> void:
	effect_callback_type = EFFECT_CALLBACK_TYPE_POST_TRANSPARENT
	access_resolved_color = true
	access_resolved_depth = true
	needs_motion_vectors = false
	_rd = RenderingServer.get_rendering_device()
	if _rd == null:
		push_warning("VSekaiOutlineJFA: no RenderingDevice; effect disabled")
		return
	_compile_shaders()


func _notification(what: int) -> void:
	if what == NOTIFICATION_PREDELETE:
		_free_resources()


func _compile_shaders() -> void:
	_shader_init  = _load_compute_shader(SHADER_DIR + "silhouette_init.glsl")
	_shader_step  = _load_compute_shader(SHADER_DIR + "jfa_step.glsl")
	_shader_final = _load_compute_shader(SHADER_DIR + "final.glsl")
	if _shader_init.is_valid():
		_pipeline_init = _rd.compute_pipeline_create(_shader_init)
	if _shader_step.is_valid():
		_pipeline_step = _rd.compute_pipeline_create(_shader_step)
	if _shader_final.is_valid():
		_pipeline_final = _rd.compute_pipeline_create(_shader_final)


func _load_compute_shader(path: String) -> RID:
	var file := load(path) as RDShaderFile
	if file == null:
		push_error("VSekaiOutlineJFA: could not load %s" % path)
		return RID()
	var spirv := file.get_spirv()
	return _rd.shader_create_from_spirv(spirv)


func _ensure_seed_textures(size: Vector2i) -> void:
	if size == _seed_size and _seed_a.is_valid():
		return
	if _seed_a.is_valid():
		_rd.free_rid(_seed_a)
	if _seed_b.is_valid():
		_rd.free_rid(_seed_b)
	var fmt := RDTextureFormat.new()
	fmt.format = RenderingDevice.DATA_FORMAT_R32G32B32A32_SINT
	fmt.width = size.x
	fmt.height = size.y
	fmt.usage_bits = (
		RenderingDevice.TEXTURE_USAGE_STORAGE_BIT
		| RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
		| RenderingDevice.TEXTURE_USAGE_CAN_COPY_TO_BIT
	)
	_seed_a = _rd.texture_create(fmt, RDTextureView.new(), [])
	_seed_b = _rd.texture_create(fmt, RDTextureView.new(), [])
	_seed_size = size


func _free_resources() -> void:
	for rid in [_seed_a, _seed_b,
				_pipeline_init, _pipeline_step, _pipeline_final,
				_shader_init, _shader_step, _shader_final]:
		if rid.is_valid():
			_rd.free_rid(rid)
	_seed_a = RID()
	_seed_b = RID()
	_seed_size = Vector2i.ZERO


func _render_callback(p_effect_callback_type: int,
					  p_render_data: RenderData) -> void:
	if p_effect_callback_type != EFFECT_CALLBACK_TYPE_POST_TRANSPARENT:
		return
	if not _pipeline_init.is_valid():
		return  # compile failed earlier; nothing to do

	var render_scene_buffers := p_render_data.get_render_scene_buffers() \
		as RenderSceneBuffersRD
	if render_scene_buffers == null:
		return

	var size := render_scene_buffers.get_internal_size()
	if size.x == 0 or size.y == 0:
		return
	_ensure_seed_textures(size)

	# TODO(CHI-255 step 2): the silhouette ID/width AOV is currently
	# expected to come from a custom render pass the application sets
	# up. Wire that pass into the buffers here, then run the three
	# dispatches below. The dispatch wiring is left intentionally
	# inert until that AOV exists so this effect is a no-op on a
	# scene without VSekaiMToonAPI materials.
	#
	# The dispatch loop, once enabled, mirrors the reference in
	# tests/slang_validate/outline_jfa_reference.py:
	#
	#   1. dispatch _pipeline_init (silhouette_init.glsl)
	#   2. for stride in [max_step, max_step/2, ..., 1, 1]:
	#         dispatch _pipeline_step (jfa_step.glsl), ping-pong _seed_a/_seed_b
	#   3. dispatch _pipeline_final (final.glsl) into resolved scene colour
