# DISCLAIMER — READ BEFORE USING

**This software may permanently damage your vehicle.**

j2534-trace intercepts and forwards diagnostic traffic between
a vehicle application and a J2534 pass-thru device. Errors in this
software, its configuration, or its interaction with your ECU may
result in:

  - Corrupted firmware or calibration ("bricked" modules)
  - Engine/transmission damage
  - Disabled safety systems (ABS, TCS, stability control)

**Backup your ECU contents before every session where writes are possible.**

## What This Software Is

- **Instrumentation tool:** Passively logs J2534 traffic
- **Research purpose:** Protocol analysis, not production tuning
- **Forwarding proxy:** Does NOT generate or modify messages itself

## What It Is NOT

- **Not a bypass tool:** Cannot unlock manufacturer security gates
- **Not tuning software:** Doesn't flash or reprogram ECUs
- **Not legal advice:** Consult counsel about emissions compliance

## Regulatory Notice

Modifying engine control software may violate:
- US: 40 CFR Part 86 (§86.1854-07 — Clean Air Act prohibition on
  emissions defeat devices)
- EU: Type approval requirements (Euro 6+, RDE regulations)
- Other jurisdictions: Local emissions/warranty laws

## Warranty & Liability

Unauthorized ECU modification may void your vehicle's powertrain
warranty. The authors accept NO responsibility for:
- Vehicle damage, towing costs, or ECU replacement expenses
- Warranty denials following ECU modification
- Regulatory violations or emissions-related penalties
- Downtime or lost productivity

## License Summary

This project uses Apache License 2.0 with additional disclaimers.
See [LICENSE](LICENSE) for full terms and [CONTRIBUTING.md](CONTRIBUTING.md)
for contribution requirements.

---

**BY DOWNLOADING OR USING THIS SOFTWARE, YOU ACKNOWLEDGE THAT YOU HAVE
READ AND UNDERSTAND THESE WARNINGS.**
