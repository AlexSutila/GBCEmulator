## Sun Apr 26 04:10:41 PM MST 2026 (+1 new fields, -5 fields)
- F_CGB_MODE removed from `runtime_sys_info`
- F_HALTED removed from `runtime_sys_info`
- F_SPEED_SWITCH_ARMED removed from `runtime_sys_info`
- F_DOUBLE_SPEED removed from `runtime_sys_info`
- F_UNMAP_KEY0 removed from `runtime_sys_info`
+ F_FLAGS added to `runtime_sys_info` (concatenates previous fields into one byte)

## Thu Apr 16 09:24:28 PM MST 2026 (+1 new field)
- F_UNMAP_KEY0 added, bumped C_GBC revision to version 3
