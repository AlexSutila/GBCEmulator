## Wed Jun  3 08:30:58 PM MST 2026 (+1 field, -1 field)
+ Unnamed system counter field added for runtyme_sys_info struct
- Removed F_SYS_COUNTER for C_TIMER

## Sun May 24 04:04:14 PM MST 2026 (+2 fields)
- F_SCHED added for C_APU
- F_SCHED added for C_PPU

## Wed May 20 05:29:04 PM MST 2026 (+2 components, ~1 component, plus general restructuring)
- C_SYS added
- C_CHILD_SCHEDULER added
- C_SCHEDULER renamed to C_SYS_SCHEDULER
- Wrapped various structures in `field_complex` and did some re-tagging to support the savestate
  tree structure better

## Sun May  3 08:50:06 PM MST 2026 (+1 component)
- C_MBC_SACHEN added, other enumeration values will have shifted

## Fri May  1 02:28:09 PM MST 2026 (~2 components)
- C_OAM_DMA refactored completely for scheduler, fields changed accordingly
- C_VDMA refactored completely for scheduler, fields changed accordingly

## Mon Apr 27 09:05:04 PM MST 2026 (~1 fields)
- F_FLAGS reordered bits in `runtime_sys_info`

## Sun Apr 26 04:10:41 PM MST 2026 (+1 new fields, -5 fields)
- F_CGB_MODE removed from `runtime_sys_info`
- F_HALTED removed from `runtime_sys_info`
- F_SPEED_SWITCH_ARMED removed from `runtime_sys_info`
- F_DOUBLE_SPEED removed from `runtime_sys_info`
- F_UNMAP_KEY0 removed from `runtime_sys_info`
+ F_FLAGS added to `runtime_sys_info` (concatenates previous fields into one byte)

## Thu Apr 16 09:24:28 PM MST 2026 (+1 new field)
- F_UNMAP_KEY0 added, bumped C_GBC revision to version 3
