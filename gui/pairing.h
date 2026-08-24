#pragma once

#include <signal.h>
#include "steam_link_pairing.h"
#include "ui.h"

/* Pairing screen. The UI remains open for the stream lifecycle. */
void svrt_pairing_gui_show(svrt_steam_link_pairing *pairing,
                           volatile sig_atomic_t *quitting, svrt_ui *ui);
