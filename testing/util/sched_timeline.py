#!/usr/bin/env python3
import sys
from collections import defaultdict

from irogb_python import (
    load_cart_filesystem,
    SchedulerComponent,
    GameBoyColor,
    BreakContext,
    BreakReason,
)

import plotly.graph_objects as go


class EventRecorder:
    def __init__(self):
        self.events = []

    def callback(self, gbc: GameBoyColor, ctx: BreakContext):
        comp, eid = ctx.data

        self.events.append(
            {
                "time": int(ctx.time),
                "component": comp,
                "event_id": int(eid),
            }
        )

        return BreakReason.BRK_CONTINUE

    def get_events(self):
        return self.events


def plot_events_plotly(events):
    if not events:
        print("No events recorded.")
        return

    lanes = sorted(
        set((e["component"], e["event_id"]) for e in events),
        key=lambda x: (x[0].name, x[1]),
    )
    lane_index = {k: i for i, k in enumerate(lanes)}
    fig = go.Figure()

    lane_points = defaultdict(lambda: {"x": [], "y": [], "text": []})

    for e in events:
        key = (e["component"], e["event_id"])
        lane = lane_index[key]

        lane_points[key]["x"].append(e["time"])
        lane_points[key]["y"].append(lane)
        lane_points[key]["text"].append(f"{key[0]}:{key[1]}<br>time={e['time']}")

    for key, pts in lane_points.items():
        fig.add_trace(
            go.Scatter(
                x=pts["x"],
                y=pts["y"],
                mode="markers",
                name=f"{key[0]}:{key[1]}",
                text=pts["text"],
                hoverinfo="text",
                marker=dict(size=6),
            )
        )

    fig.update_layout(
        title="GameBoy Emulator Event Timeline",
        xaxis_title="Cycle time",
        yaxis_title="Event lane",
        hovermode="closest",
        height=700,
    )

    fig.update_yaxes(
        tickmode="array",
        tickvals=list(range(len(lanes))),
        ticktext=[f"{c.name if hasattr(c, 'name') else c}:{
            eid}" for c, eid in lanes],
    )

    fig.show()


def run(cart_path: str, steps: int = 120):
    cart = load_cart_filesystem(cart_path)

    recorder = EventRecorder()

    def breakpoint_cb(gbc, ctx):
        return recorder.callback(gbc, ctx)

    gbc = GameBoyColor(cartridge=cart, dbg_callback=breakpoint_cb)
    components = [SchedulerComponent.VRAM_DMA, SchedulerComponent.OAM_DMA]

    for component in components:
        for i in range(256):  # Brute force guess how many events there are lol
            gbc.debugger.breakpoint_add_event(
                (component, i), BreakReason.BRK_EVENT_POPPED
            )

    print("[*] Running emulator...")

    for _ in range(steps):
        gbc.step_frame(big_step=True)

    print(f"[*] Events captured: {len(recorder.events)}")
    plot_events_plotly(recorder.get_events())


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: ./trace_plotly.py <rom> [steps]")
        sys.exit(1)

    rom = sys.argv[1]
    steps = int(sys.argv[2]) if len(sys.argv) > 2 else 120

    run(rom, steps)
