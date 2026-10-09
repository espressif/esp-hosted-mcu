Put the coprocessor image you want to end up running here, as one `.bin`.

`idf.py flash` packs this directory into the host's `cp_fw` LittleFS partition,
and `phy_cert_cp_ota` streams the image from there to the coprocessor.

Nothing here is committed: a certification example must not carry coprocessor
firmware. Normally this is your **production** build, not the rf_cert one — the
point is to leave RF test mode behind.

Only used when `CONFIG_RF_CERT_EXAMPLE_INTEGRATED_OTA` is on.
