extends SceneTree
## Headless tests for MCPCommandBuffer (constant-work ring buffer).
## Run: godot --headless --path addons/godot-mcp --script res://tests/test_command_buffer.gd

const Buffer = preload("res://addons/godot_mcp/mcp_command_buffer.gd")

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
	b.enqueue("a"); b.enqueue("b"); b.enqueue("c")
	_check(b.pending() == 3, "pending after 3 enqueue")
	_check(b.drain(10) == ["a", "b", "c"], "drain FIFO order")
	_check(b.pending() == 0 and b.is_empty(), "empty after full drain")

	# capacity + backpressure
	var c = Buffer.new(4)
	for i in 4:
		_check(c.enqueue(i), "enqueue within capacity (%d)" % i)
	_check(c.is_full(), "is_full at capacity")
	_check(not c.enqueue(99), "enqueue past capacity rejected")
	_check(c.dropped == 1, "dropped counter bumped")
	_check(c.pending() == 4, "pending stays at capacity")

	# constant drain budget — never returns more than budget
	var d = Buffer.new(16)
	for i in 10:
		d.enqueue(i)
	var first := d.drain(4)
	_check(first.size() == 4 and first == [0, 1, 2, 3], "drain budget=4 yields exactly 4 FIFO")
	_check(d.pending() == 6, "pending reduced by budget")
	_check(d.drain(0).is_empty(), "drain(0) is a no-op")
	_check(d.pending() == 6, "drain(0) leaves buffer unchanged")
	var rest := d.drain(100)
	_check(rest.size() == 6 and rest[0] == 4 and rest[5] == 9, "drain > pending yields remainder")

	# wraparound: fill, partial drain, refill across the ring boundary
	var w = Buffer.new(3)
	w.enqueue("x"); w.enqueue("y"); w.enqueue("z")   # full
	_check(w.drain(2) == ["x", "y"], "wrap: drain 2")
	w.enqueue("p"); w.enqueue("q")                   # head wraps past 0
	_check(w.pending() == 3, "wrap: pending after refill")
	_check(w.drain(10) == ["z", "p", "q"], "wrap: FIFO preserved across boundary")

	# constant-work invariant across random-ish budgets
	var e = Buffer.new(32)
	for i in 20:
		e.enqueue(i)
	var total := 0
	for budget in [3, 7, 1, 50]:
		total += e.drain(budget).size()
	_check(total == 20, "all items drained exactly once across varied budgets")
	_check(e.is_empty(), "buffer empty at end")
