extends SceneTree
## Headless tests for MCPCommandBuffer (constant-work ring buffer).
## Run: godot --headless --path . --script res://tests/test_command_buffer.gd

const Buffer = preload("res://addons/vsekai_godot_mcp/mcp_command_buffer.gd")

var _fail := 0
var _pass := 0


func _initialize() -> void:
	_run()
	print("\n[godot_mcp buffer tests] %d passed, %d failed" % [_pass, _fail])
	quit(1 if _fail > 0 else 0)


func _check(cond: bool, msg: String) -> void:
	if cond:
		_pass += 1
	else:
		_fail += 1
		printerr("  FAIL: ", msg)


func _run() -> void:
	# FIFO order
	var b = Buffer.new(8)
	b.record("a"); b.record("b"); b.record("c")
	_check(b.pending() == 3, "pending after 3 record")
	_check(b.submit(10) == ["a", "b", "c"], "submit FIFO order")
	_check(b.pending() == 0 and b.is_empty(), "empty after full submit")

	# capacity + backpressure
	var c = Buffer.new(4)
	for i in 4:
		_check(c.record(i), "record within capacity (%d)" % i)
	_check(c.is_full(), "is_full at capacity")
	_check(not c.record(99), "record past capacity rejected")
	_check(c.dropped == 1, "dropped counter bumped")
	_check(c.pending() == 4, "pending stays at capacity")

	# constant submit budget — never returns more than budget
	var d = Buffer.new(16)
	for i in 10:
		d.record(i)
	var first := d.submit(4)
	_check(first.size() == 4 and first == [0, 1, 2, 3], "submit budget=4 yields exactly 4 FIFO")
	_check(d.pending() == 6, "pending reduced by budget")
	_check(d.submit(0).is_empty(), "submit(0) is a no-op")
	_check(d.pending() == 6, "submit(0) leaves buffer unchanged")
	var rest := d.submit(100)
	_check(rest.size() == 6 and rest[0] == 4 and rest[5] == 9, "submit > pending yields remainder")

	# wraparound: fill, partial submit, refill across the ring boundary
	var w = Buffer.new(3)
	w.record("x"); w.record("y"); w.record("z")   # full
	_check(w.submit(2) == ["x", "y"], "wrap: submit 2")
	w.record("p"); w.record("q")                   # head wraps past 0
	_check(w.pending() == 3, "wrap: pending after refill")
	_check(w.submit(10) == ["z", "p", "q"], "wrap: FIFO preserved across boundary")

	# constant-work invariant across random-ish budgets
	var e = Buffer.new(32)
	for i in 20:
		e.record(i)
	var total := 0
	for budget in [3, 7, 1, 50]:
		total += e.submit(budget).size()
	_check(total == 20, "all items drained exactly once across varied budgets")
	_check(e.is_empty(), "buffer empty at end")
