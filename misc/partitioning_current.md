# Storage ownership

SysUtils startup and first boot only discover and mount existing filesystems.
They never resize, create or format partitions. Camera and runtime configuration
still run on first boot. ImageWriter prepares recording storage during a full
flash, before publishing the partition table and reporting success. Update
uploads do not provision storage.

Explicit GUI storage actions and the legacy resize request handler remain
available. Their formatting is destructive and must be explicitly requested.
The operation-owned startup hold cleanup must be preserved.

Legacy startup helpers under `old/` also have automatic storage mutation removed,
including sentinel-triggered Pi/Rock/X20 and UUID-based x86 resizing.

The Luckfox/X21 periodic blank-SD scan and automatic repartitioning worker are
removed as well. Card insertion only exposes GUI storage choices. Legacy manual
resize requests require measured geometry and an unmounted final RECORDINGS FAT
partition; they cannot select OPENHD, unknown or intermediate partitions.
