#ifndef LAMBO_FRONTEND_SETTINGS_H
#define LAMBO_FRONTEND_SETTINGS_H

namespace lambo::ui {
void create_frontend_settings();
// Called under the frontend lock. Force on opens/queued page changes. Returns
// true when schema values were checked; false for an unchanged, clean menu.
bool refresh_frontend_settings(bool force = false);
}

#endif
