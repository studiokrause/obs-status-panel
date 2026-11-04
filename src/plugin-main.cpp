#include <obs-module.h>

#include "status-panel.hpp"
#include "plugin-version.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")
OBS_MODULE_AUTHOR("Andrzej Krause")

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Status Panel";
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

bool obs_module_load(void)
{
	/* The dock is registered at load time so OBS restores its position
	 * together with the rest of the window layout. */
	StatusPanel::Register();
	blog(LOG_INFO, "[%s] loaded, version %s", PLUGIN_NAME, PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void) {}
