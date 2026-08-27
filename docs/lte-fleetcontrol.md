# FleetControl LTE profile

FleetControl provisions a vehicle with `POST /api/vpn/vehicles/:vehicleId`.
The returned `openhd-lte.conf` is a normal `wg-quick` profile with OpenHD
routing metadata in leading comments.

Install that file as `/etc/wireguard/openhd-lte.conf`, owned by root and mode
`0600` (SysUtils refuses less restrictive key permissions), then add these
fields to the SysUtils `config.json`:

```json
{
  "lte_enabled": true,
  "lte_wireguard_config": "/etc/wireguard/openhd-lte.conf"
}
```

SysUtils activates the tunnel at startup. It never returns or logs the private
key; OpenHD receives only the device ID, VPN server address, interface state,
and assigned UDP ports through the local settings socket.
