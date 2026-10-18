package api

import (
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestUIStationPickerHooks(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	if rr.Code != 200 {
		t.Fatalf("GET / %d", rr.Code)
	}
	body := rr.Body.String()
	for _, need := range []string{
		"data-st-pick",
		"Choose a station",
		"In this program",
		"removed: false",
		"function stepMinutes(",
		"function clampStepMinutes(",
		"All stations added",
		"Remove ${esc(name)} from this program",
		"if (delta < 0 && cur <= 1)",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("ui missing %q", need)
		}
	}
	if strings.Contains(body, "row.remove()") {
		t.Fatal("stepper must not delete a row")
	}
}

func TestUIStepLogicWithNode(t *testing.T) {
	if _, err := exec.LookPath("node"); err != nil {
		t.Skip("node is not installed")
	}
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	if rr.Code != 200 {
		t.Fatalf("GET / %d", rr.Code)
	}
	body := rr.Body.String()
	const start = "/* zan-pure:start */"
	const end = "/* zan-pure:end */"
	i := strings.Index(body, start)
	j := strings.Index(body, end)
	if i < 0 || j <= i {
		t.Fatal("pure block markers missing")
	}
	src := body[i+len(start) : j]
	harness := src + "\n" + nodeStepHarness
	dir := t.TempDir()
	path := filepath.Join(dir, "pure.js")
	if err := os.WriteFile(path, []byte(harness), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := exec.Command("node", path).CombinedOutput()
	if err != nil {
		t.Fatalf("node: %v\n%s", err, out)
	}
	if !strings.Contains(string(out), "ok") {
		t.Fatalf("node output %s", out)
	}
}

const nodeStepHarness = `
const assert = require("assert");
assert.deepStrictEqual(stepMinutes(1, -1), { minutes: 1, removed: false });
assert.deepStrictEqual(stepMinutes(1, -40), { minutes: 1, removed: false });
assert.strictEqual(stepMinutes(2, -1).minutes, 1);
assert.strictEqual(stepMinutes(2, -1).removed, false);
assert.strictEqual(stepMinutes(6, 1).minutes, 7);
assert.strictEqual(stepMinutes(180, 5).minutes, 180);
const added = addStationRow([{ station_id: "lawn-a", minutes: 6 }], ["lawn-a", "trees", "beds"]);
assert.strictEqual(added.added, true);
assert.strictEqual(added.rows.length, 2);
assert.strictEqual(added.rows[0].station_id, "lawn-a");
assert.strictEqual(added.rows[0].minutes, 6);
assert.strictEqual(added.rows[1].station_id, "trees");
assert.strictEqual(added.rows[1].minutes, 6);
const none = addStationRow([{ station_id: "lawn-a", minutes: 1 }], ["lawn-a"]);
assert.strictEqual(none.added, false);
assert.strictEqual(none.rows.length, 1);
const pick = stationsForPicker(
  [
    { id: "lawn-a", name: "Lawn A" },
    { id: "lawn-b", name: "Lawn B" },
    { id: "beds", name: "Beds" },
    { id: "trees", name: "Trees" },
  ],
  [
    { station_id: "lawn-a", minutes: 6 },
    { station_id: "lawn-b", minutes: 6 },
    { station_id: "beds", minutes: 6 },
  ],
  1
);
assert.strictEqual(pick[1].state, "current");
assert.strictEqual(pick[1].note, "Current");
assert.strictEqual(pick[0].state, "disabled");
assert.strictEqual(pick[0].note, "In this program");
assert.strictEqual(pick[2].note, "In this program");
assert.strictEqual(pick[3].state, "open");
assert.deepStrictEqual(weekDisplayOrder(7), [6, 0, 1, 2, 3, 4, 5]);
assert.deepStrictEqual(weekDisplayOrder(1), [0, 1, 2, 3, 4, 5, 6]);
assert.deepStrictEqual(weekDisplayOrder(99), [6, 0, 1, 2, 3, 4, 5]);
assert.deepStrictEqual(canonicalWeekdays(["sun", "mon", "wed"]), ["mon", "wed", "sun"]);
assert.deepStrictEqual(canonicalWeekdays(["mon", "tue", "wed", "thu", "fri", "sat", "sun"]), []);
assert.strictEqual(formatStartLabel("06:10"), "6:10 AM");
assert.strictEqual(formatStartLabel("00:00"), "12:00 AM");
assert.strictEqual(formatStartLabel("12:00"), "12:00 PM");
assert.strictEqual(formatStartLabel("18:05"), "6:05 PM");
assert.strictEqual(formatSeasonRange("2026-05-01", "2026-09-30"), "May 1\u2013Sep 30");
assert.strictEqual(formatSeasonRange("", ""), "All year");
assert.strictEqual(historyDetailText("skipped", "rain", [{ name: "Lawn A", sec: 360 }]), "No stations ran · paused for rain");
assert.strictEqual(historyDetailText("skipped", "wind", [{ name: "Beds", sec: 0 }]), "No stations ran · wind");
assert.strictEqual(historyDetailText("skipped", "rain", []), "");
assert.strictEqual(historyDetailText("failed", "", [{ name: "Beds", sec: 0 }, { name: "Trees", sec: 180 }]), "Beds 0 min · Trees 3 min");
assert.strictEqual(historyDetailText("failed", "", []), "");
assert.strictEqual(historyDetailText("stopped", "", [{ name: "Trees", sec: 0 }]), "Trees 0 min");
console.log("ok");
`
