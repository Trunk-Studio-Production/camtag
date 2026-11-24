#pragma once
// Versione del firmware: compare sulla schermata Bluetooth e nella risposta a
// HELLO (docs/bluetooth.md). Il configuratore web si aspetta la stessa
// (`FW_VERSION` in companion/index.html): cambiale insieme.
// La build di prova con la diagnosi da spento già accesa (docs/batteria.md,
// -DCAMTAG_OFF_DIAG_DEFAULT=1) porta "-diag" in coda, per non scambiarla con quella
// pubblicata: lo si legge sulla schermata Bluetooth dell'X3 e nel configuratore web.
#ifndef CAMTAG_OFF_DIAG_DEFAULT
#define CAMTAG_OFF_DIAG_DEFAULT 0
#endif
#if CAMTAG_OFF_DIAG_DEFAULT
#define CAMTAG_FW_SUFFIX "-diag"
#else
#define CAMTAG_FW_SUFFIX ""
#endif
#define CAMTAG_FW_VERSION "0.5.7" CAMTAG_FW_SUFFIX
// Nel binario come "CAMTAG_FW=<versione>": il configuratore web lo cerca nel .bin
// prima di mandarlo all'X3 (aggiornamento via Bluetooth, docs/bluetooth.md).
#define CAMTAG_FW_TAG "CAMTAG_FW=" CAMTAG_FW_VERSION
