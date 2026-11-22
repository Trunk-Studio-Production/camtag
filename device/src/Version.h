#pragma once
// Versione del firmware: compare sulla schermata Bluetooth e nella risposta a
// HELLO (docs/bluetooth.md). Il configuratore web si aspetta la stessa
// (`FW_VERSION` in companion/index.html): cambiale insieme.
#define CAMTAG_FW_VERSION "0.5.1"
// Nel binario come "CAMTAG_FW=<versione>": il configuratore web lo cerca nel .bin
// prima di mandarlo all'X3 (aggiornamento via Bluetooth, docs/bluetooth.md).
#define CAMTAG_FW_TAG "CAMTAG_FW=" CAMTAG_FW_VERSION
