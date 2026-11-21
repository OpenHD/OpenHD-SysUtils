# Storage ownership

SysUtils startup and first boot only discover and mount existing filesystems.
They never resize, create or format partitions. Camera and runtime configuration
still run on first boot. ImageWriter prepares recording storage during a full
flash, before publishing the partition table and reporting success. Update
uploads do not provision storage.

SysUtils continues to expose explicit GUI storage operations for existing
systems. Formatting requires a user request. The legacy first-boot resize
request returns `accepted: false` and disk inventory still reports the
`resizable` field for compatibility.

Legacy startup helpers under `old/` also have automatic storage mutation removed,
including sentinel-triggered Pi/Rock/X20 and UUID-based x86 resizing.

The Luckfox/X21 periodic blank-SD scan and automatic repartitioning worker are
removed as well. Card insertion only exposes GUI storage choices.
