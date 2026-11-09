/* screencast-portal.c
 *
 * Copyright 2022 Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Modified by Denis Kopyrin to support capture via mutter inject.
 */

#include "mutter-capture.h"

#include "pipewire.h"

#include <gio/gio.h>
#include <json-glib/json-glib.h>

#include <algorithm>
#include <variant>
#include <vector>
#include <optional>
#include <string>

struct plug_t {
	std::string connector;
	std::string monitor;

	bool operator<(const plug_t &other) const
	{
		if (connector < other.connector)
			return true;
		if (connector > other.connector)
			return false;

		return monitor < other.monitor;
	}
};

struct window_t {
	guint64 id;
	std::string title;
	std::string wm_class;

	bool operator<(const window_t &other) const
	{
		if (title < other.title)
			return true;
		if (title > other.title)
			return false;

		return wm_class < other.wm_class;
	}
};

struct mutter_capture {
	GCancellable *cancellable = nullptr;

	obs_source_t *source = nullptr;
	obs_data_t *settings = nullptr;

	std::string config;
	bool cursor_visible;

	std::string session;

	obs_pipewire *obs_pw = nullptr;
	obs_pipewire_stream *obs_pw_stream = nullptr;

	GSource *rescan_sources = nullptr;

	std::string props_window_str;
};

/* ------------------------------------------------- */

struct Config {
	std::string encoding;
	bool mouse;
};

#define SEPARATOR "\r\n"

static std::string encodePlug(const plug_t &plug)
{
	return std::string{"M"} + plug.connector;
}

static std::string encodeWindow(const window_t &wnd)
{
	return std::string{"W"} + wnd.title + SEPARATOR + wnd.wm_class + SEPARATOR + std::to_string(wnd.id);
}

struct PlugDecode {
	std::string connector;
};

struct WindowDecode {
	std::string title;
	std::string wm_class;
	uint64_t id;
};

struct BadDecode {};

static std::variant<BadDecode, PlugDecode, WindowDecode> decode(const std::string &val)
{
	if (val.empty())
		return BadDecode{};

	switch (val[0]) {
	case 'M':
		return PlugDecode{&val[1]};

	case 'W': {
		size_t loc0 = val.find(SEPARATOR);
		if (loc0 == std::string::npos)
			return BadDecode{};

		std::string title = val.substr(1, loc0);

		size_t loc1 = val.find(SEPARATOR, loc0 + 1);
		if (loc1 == std::string::npos)
			return BadDecode{};

		std::string wm_class = val.substr(loc0 + 2, loc1 - (loc0 + 2));
		std::string number = val.substr(loc1 + 2);

		uint64_t id = strtoul(number.c_str(), nullptr, 0);

		return WindowDecode{std::move(title), std::move(wm_class), id};
	}

	default:
		return BadDecode{};

		break;
	}
}

/* ------------------------------------------------- */

static std::vector<window_t> update_windows()
{
	std::vector<window_t> windows;

	char path[128];
	snprintf(path, sizeof(path), "/run/user/%d/obs_windows.json", getuid());

	GError *err = NULL;
	gchar *contents = NULL;
	if (!g_file_get_contents(path, &contents, NULL, &err)) {
		g_error_free(err);
		return {};
	}

	JsonParser *parser = json_parser_new();
	if (!json_parser_load_from_data(parser, contents, -1, &err)) {
		blog(LOG_ERROR, "Cannot parse %s: %s", path, err->message);
		g_error_free(err);
		g_free(contents);
		g_object_unref(parser);
		return {};
	}
	g_free(contents);

	JsonNode *root = json_parser_get_root(parser);
	JsonArray *array = json_node_get_array(root);
	if (array == NULL) {
		blog(LOG_ERROR, "%s does not contain a JSON array", path);
		g_object_unref(parser);
		return {};
	}

	gint len = json_array_get_length(array);
	for (gint i = 0; i < len; i++) {
		JsonObject *obj = json_array_get_object_element(array, i);
		if (!obj)
			continue;

		window_t window;
		window.id = json_object_get_int_member(obj, "id");
		if (const char *title = json_object_get_string_member(obj, "title"))
			window.title = title;
		if (const char *wm_class = json_object_get_string_member(obj, "class"))
			window.wm_class = wm_class;

		windows.push_back(std::move(window));
	}

	g_object_unref(parser);

	return windows;
}

static std::string get_windows_string()
{
	std::vector<window_t> windows;

	char path[128];
	snprintf(path, sizeof(path), "/run/user/%d/obs_windows.json", getuid());

	GError *err = NULL;
	gchar *contents = NULL;
	if (!g_file_get_contents(path, &contents, NULL, &err)) {
		g_error_free(err);
		return {};
	}

	std::string res = contents;
	g_free(contents);

	return res;
}

/* ------------------------------------------------- */

static GDBusConnection *connection = NULL;

static void ensure_connection(void)
{
	g_autoptr(GError) error = NULL;
	if (!connection) {
		connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);

		if (error) {
			blog(LOG_WARNING, "[portals] Error retrieving D-Bus connection: %s", error->message);
			return;
		}
	}
}

static GDBusConnection *portal_get_dbus_connection(void)
{
	ensure_connection();
	return connection;
}

/* ------------------------------------------------- */

struct signal_context {
	std::string session;
	std::string stream_path;

	GCancellable *cancellable;
	guint close_id;
	guint start_id;
	gulong cancel_id;

	struct mutter_capture *capture;
};

static void delete_signal_context(struct signal_context *ctx)
{
	if (ctx->close_id) {
		g_dbus_connection_signal_unsubscribe(portal_get_dbus_connection(), ctx->close_id);
		ctx->close_id = 0;
	}

	if (ctx->start_id) {
		g_dbus_connection_signal_unsubscribe(portal_get_dbus_connection(), ctx->start_id);
		ctx->start_id = 0;
	}

	if (ctx->cancel_id > 0) {
		g_signal_handler_disconnect(ctx->cancellable, ctx->cancel_id);
		ctx->cancel_id = 0;
	}

	delete ctx;
}

static bool reload_session(struct mutter_capture *capture);
static void on_pw_stream_dropped(void *user_data)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(user_data);
	reload_session(capture);
}

static void on_start_response_received_cb(GDBusConnection * /*connection*/, const gchar * /*sender_name*/,
					  const gchar * /*object_path*/, const gchar * /*interface_name*/,
					  const gchar * /*signal_name*/, GVariant *parameters, gpointer user_data)
{
	struct signal_context *ctx = reinterpret_cast<struct signal_context *>(user_data);
	struct mutter_capture *capture = ctx->capture;

	g_dbus_connection_signal_unsubscribe(portal_get_dbus_connection(), ctx->start_id);
	ctx->start_id = 0;

	guint node_id = 0;
	g_variant_get(parameters, "(u)", &node_id, NULL);

	if (capture->session != ctx->session)
		return;

	blog(LOG_INFO, "[mutter] source selected, setting up screencast");

	capture->obs_pw = obs_pipewire_connect(NULL, NULL);

	if (!capture->obs_pw)
		return;

	obs_pipewire_connect_stream_info connect_info = {
		.stream_name = "OBS Studio",
		.stream_properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
						       PW_KEY_MEDIA_ROLE, "Screen", NULL),
		.stream_dropped = on_pw_stream_dropped,
		.stream_dropped_user_data = capture,
		.screencast =
			{
				.cursor_visible = capture->cursor_visible,
			},
	};

	capture->obs_pw_stream = obs_pipewire_connect_stream(capture->obs_pw, capture->source, node_id, &connect_info);
}

static void on_close_received_cb(GDBusConnection * /*connection*/, const gchar * /*sender_name*/,
				 const gchar * /*object_path*/, const gchar * /*interface_name*/,
				 const gchar * /*signal_name*/, GVariant * /*parameters*/, gpointer user_data)
{
	struct signal_context *ctx = reinterpret_cast<struct signal_context *>(user_data);
	struct mutter_capture *capture = ctx->capture;
	delete_signal_context(ctx);

	if (capture->session != ctx->session)
		return;

	blog(LOG_INFO, "[portals] Source closed");
	g_clear_pointer(&capture->obs_pw_stream, obs_pipewire_stream_destroy);
	g_clear_pointer(&capture->obs_pw, obs_pipewire_destroy);
}

static void on_cancelled_cb(GCancellable *cancellable, void *user_data)
{
	UNUSED_PARAMETER(cancellable);

	struct signal_context *ctx = reinterpret_cast<struct signal_context *>(user_data);
	std::string session = std::move(ctx->session);
	delete_signal_context(ctx);

	blog(LOG_INFO, "[portals] Request cancelled");

	g_dbus_connection_call(connection, "org.gnome.Mutter.ScreenCast", session.c_str(),
			       "org.gnome.Mutter.ScreenCast.Session", "Stop", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
			       NULL, NULL, NULL);
}

static void on_started_cb(GObject *source, GAsyncResult *res, void *user_data)
{
	UNUSED_PARAMETER(user_data);

	g_autoptr(GVariant) result = NULL;
	g_autoptr(GError) error = NULL;

	result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR, "[mutter] Error selecting screencast source: %s", error->message);
		return;
	}
}

static void start(struct mutter_capture *capture, const char *stream_path)
{
	struct signal_context *ctx = new signal_context;
	ctx->session = capture->session;
	ctx->stream_path = stream_path;
	ctx->capture = capture;
	ctx->close_id = g_dbus_connection_signal_subscribe(connection, NULL, NULL, "Closed", capture->session.c_str(),
							   NULL, (GDBusSignalFlags)G_DBUS_CALL_FLAGS_NONE,
							   on_close_received_cb, ctx, NULL);

	ctx->start_id = g_dbus_connection_signal_subscribe(connection, NULL, NULL, "PipeWireStreamAdded",
							   ctx->stream_path.c_str(), NULL,
							   (GDBusSignalFlags)G_DBUS_CALL_FLAGS_NONE,
							   on_start_response_received_cb, ctx, NULL);

	ctx->cancellable = g_object_ref(capture->cancellable);
	ctx->cancel_id = g_signal_connect(ctx->cancellable, "cancelled", G_CALLBACK(on_cancelled_cb), ctx);

	g_dbus_connection_call(connection, "org.gnome.Mutter.ScreenCast", capture->session.c_str(),
			       "org.gnome.Mutter.ScreenCast.Session", "Start", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
			       capture->cancellable, on_started_cb, NULL);
}

/* ------------------------------------------------- */

struct async_context {
	std::string session;
	struct mutter_capture *capture;
};

static void on_cancelled_async_cb(GCancellable *cancellable, void *user_data)
{
	UNUSED_PARAMETER(cancellable);

	struct signal_context *ctx = reinterpret_cast<struct signal_context *>(user_data);
	delete_signal_context(ctx);

	blog(LOG_INFO, "[portals] Async cancelled");

	g_dbus_connection_call(connection, "org.gnome.Mutter.ScreenCast", ctx->session.c_str(),
			       "org.gnome.Mutter.ScreenCast.Session", "Stop", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1,
			       NULL, NULL, NULL);
}

static struct mutter_capture *down_ctx_to_mutter_capture(struct async_context *ctx)
{
	struct mutter_capture *capture = ctx->capture;
	bool ok = capture->session == ctx->session;
	delete ctx;

	if (ok)
		return capture;
	else
		return NULL;
}

static void defer_select_source(struct mutter_capture *capture);
static void on_source_selected_cb(GObject *source, GAsyncResult *res, void *user_data)
{
	struct async_context *ctx = reinterpret_cast<struct async_context *>(user_data);

	g_autoptr(GVariant) result = NULL;
	g_autoptr(GError) error = NULL;

	struct mutter_capture *capture = down_ctx_to_mutter_capture(ctx);

	result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			if (capture)
				defer_select_source(capture);
		}

		return;
	}

	if (!capture)
		return;

	gchar *stream_path = NULL;
	g_variant_get(result, "(o)", &stream_path, NULL);

	if (!stream_path) {
		if (capture)
			defer_select_source(capture);

		return;
	}

	start(capture, stream_path);
}

static std::optional<uint64_t> find_window_id(const std::string &wm_class, uint64_t id)
{
	auto windows = update_windows();

	for (const auto &wnd : windows) {
		if (wnd.id == id && wnd.wm_class == wm_class)
			return wnd.id;
	}

	for (const auto &wnd : windows) {
		if (wnd.wm_class == wm_class)
			return wnd.id;
	}

	return std::nullopt;
}

static bool try_select_source(struct mutter_capture *capture)
{
	auto decodedWnd = decode(capture->config);
	if (auto plug = std::get_if<PlugDecode>(&decodedWnd)) {
		struct async_context *ctx = new async_context;
		ctx->capture = capture;
		ctx->session = capture->session;

		g_dbus_connection_call(connection, "org.gnome.Mutter.ScreenCast", capture->session.c_str(),
				       "org.gnome.Mutter.ScreenCast.Session", "RecordMonitor",
				       g_variant_new_parsed("(%s, {'cursor-mode' : <%u>})", plug->connector.c_str(),
							    capture->cursor_visible),
				       NULL, G_DBUS_CALL_FLAGS_NONE, -1, capture->cancellable, on_source_selected_cb,
				       ctx);
	} else if (auto wnd = std::get_if<WindowDecode>(&decodedWnd)) {
		std::optional<uint64_t> window_id = find_window_id(wnd->wm_class, wnd->id);
		if (!window_id)
			return false;

		struct async_context *ctx = new async_context;
		ctx->capture = capture;
		ctx->session = capture->session;

		g_dbus_connection_call(connection, "org.gnome.Mutter.ScreenCast", capture->session.c_str(),
				       "org.gnome.Mutter.ScreenCast.Session", "RecordWindow",
				       g_variant_new_parsed("({'window-id' : <%t>, 'cursor-mode' : <%u>},)", *window_id,
							    capture->cursor_visible),
				       NULL, G_DBUS_CALL_FLAGS_NONE, -1, capture->cancellable, on_source_selected_cb,
				       ctx);
	} else {
		return false;
	}

	return true;
}

static gboolean select_source_rescan(gpointer data)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(data);
	if (try_select_source(capture)) {
		g_clear_pointer(&capture->rescan_sources, g_source_unref);
		return G_SOURCE_REMOVE;
	} else {
		return G_SOURCE_CONTINUE;
	}
}

static void defer_select_source(struct mutter_capture *capture)
{
	capture->rescan_sources = g_timeout_source_new_seconds(2);
	g_source_set_callback(capture->rescan_sources, select_source_rescan, capture, NULL);
	g_source_attach(capture->rescan_sources, NULL);
}

static void select_source(struct mutter_capture *capture)
{
	if (!try_select_source(capture)) {
		blog(LOG_INFO, "[mutter] deferring for next rescan");
		defer_select_source(capture);
	}
}

/* ------------------------------------------------- */

static void on_session_created_cb(GObject *source, GAsyncResult *res, void *user_data)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(user_data);

	g_autoptr(GVariant) result = NULL;
	g_autoptr(GError) error = NULL;

	result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), res, &error);
	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR, "[mutter] Error creating screencast session: %s", error->message);
		return;
	}

	gchar *session_path;
	g_variant_get(result, "(o)", &session_path, NULL);

	if (!capture->session.empty()) {
		g_dbus_connection_call(connection, "org.gnome.Mutter.ScreenCast", session_path,
				       "org.gnome.Mutter.ScreenCast.Session", "Stop", NULL, NULL,
				       G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
		return;
	}

	capture->session = session_path;

	select_source(capture);
}

static void create_session(struct mutter_capture *capture)
{
	g_dbus_connection_call(connection, "org.gnome.Mutter.ScreenCast", "/org/gnome/Mutter/ScreenCast",
			       "org.gnome.Mutter.ScreenCast", "CreateSession",
			       g_variant_new_parsed("({'dummy' : <0>},)"), NULL, G_DBUS_CALL_FLAGS_NONE, -1,
			       capture->cancellable, on_session_created_cb, capture);
}

/* ------------------------------------------------- */

static gboolean init_screencast_capture(struct mutter_capture *capture)
{
	if (capture->rescan_sources) {
		g_source_destroy(capture->rescan_sources);
		g_clear_pointer(&capture->rescan_sources, g_source_unref);
	}
	capture->cancellable = g_cancellable_new();
	if (!portal_get_dbus_connection())
		return FALSE;

	blog(LOG_INFO, "DBus initialized");

	create_session(capture);

	return TRUE;
}

static bool reload_session(struct mutter_capture *capture)
{
	g_clear_pointer(&capture->obs_pw_stream, obs_pipewire_stream_destroy);
	g_clear_pointer(&capture->obs_pw, obs_pipewire_destroy);

	if (capture->session.c_str()) {
		blog(LOG_DEBUG, "[mutter] Cleaning previous session %s", capture->session.c_str());

		g_dbus_connection_call(connection, "org.gnome.Mutter.ScreenCast", capture->session.c_str(),
				       "org.gnome.Mutter.ScreenCast.Session", "Stop", NULL, NULL,
				       G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

		capture->session.clear();
		g_cancellable_cancel(capture->cancellable);
		g_clear_object(&capture->cancellable);
	}

	init_screencast_capture(capture);

	return false;
}

/* obs_source_info methods */

static const char *mutter_capture_get_name(void *data)
{
	UNUSED_PARAMETER(data);
	return obs_module_text("GNOME Mutter Screen Cast");
}

static void *mutter_capture_create(obs_data_t *settings, obs_source_t *source)
{
	struct mutter_capture *capture;

	capture = new mutter_capture;
	capture->cursor_visible = obs_data_get_bool(settings, "cursor");
	capture->config = obs_data_get_string(settings, "source");
	capture->source = source;

	init_screencast_capture(capture);

	return capture;
}

static void mutter_capture_destroy(void *data)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(data);

	if (!capture)
		return;

	if (!capture->session.empty()) {
		g_dbus_connection_call(portal_get_dbus_connection(), "org.gnome.Mutter.ScreenCast",
				       capture->session.c_str(), "org.gnome.Mutter.ScreenCast.Session", "Stop", NULL,
				       NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);

		capture->session.clear();
	}

	g_clear_pointer(&capture->obs_pw_stream, obs_pipewire_stream_destroy);
	obs_pipewire_destroy(capture->obs_pw);
	g_cancellable_cancel(capture->cancellable);
	g_clear_object(&capture->cancellable);

	if (capture->rescan_sources) {
		g_source_destroy(capture->rescan_sources);
		g_clear_pointer(&capture->rescan_sources, g_source_unref);
	}

	delete capture;
}

static void mutter_capture_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "source", "");
	obs_data_set_default_bool(settings, "cursor", true);
	// obs_data_set_default_bool(settings, "timestamps", false);
}

static std::vector<plug_t> update_plugs(void)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) result = g_dbus_connection_call_sync(connection, "org.gnome.Mutter.DisplayConfig",
								 "/org/gnome/Mutter/DisplayConfig",
								 "org.gnome.Mutter.DisplayConfig", "GetCurrentState",
								 NULL, NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);

	if (error) {
		if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			blog(LOG_ERROR, "[mutter] Error enumerating monitors: %s", error->message);
		return {};
	}

	GVariant *list;

	g_variant_get(result, "(u@a((ssss)a(siiddada{sv})a{sv})a(iiduba(ssss)a{sv})a{sv})", NULL, &list, NULL, NULL);

	GVariantIter iter;
	g_variant_iter_init(&iter, list);

	gchar *connector;
	gchar *monitor;

	std::vector<plug_t> plugs;
	while (g_variant_iter_loop(&iter, "((ssss)a(siiddada{sv})a{sv})", &connector, NULL, &monitor, NULL, NULL,
				   NULL)) {
		plug_t plug;
		plug.connector = connector;
		plug.monitor = monitor;

		plugs.push_back(std::move(plug));
	}

	g_variant_unref(list);

	return plugs;
}

static void insert_preserved_val(obs_property_t *p, const char *val, size_t idx)
{
	auto res = decode(val);

	if (auto plug = std::get_if<PlugDecode>(&res)) {
		obs_property_list_insert_string(p, idx, plug->connector.c_str(), val);
	}

	if (auto wnd = std::get_if<WindowDecode>(&res)) {
		obs_property_list_insert_string(p, idx, wnd->title.c_str(), val);
	}

	obs_property_list_item_disable(p, idx, true);
}

static bool ms_check_window_property_setting(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings,
					     const char *val, size_t idx)
{
	const char *cur_val;
	bool match = false;
	size_t i = 0;

	cur_val = obs_data_get_string(settings, val);
	if (!cur_val) {
		return false;
	}

	for (;;) {
		const char *xval = obs_property_list_item_string(p, i++);
		if (!xval)
			break;

		if (strcmp(xval, cur_val) == 0) {
			match = true;
			break;
		}
	}

	if (cur_val && *cur_val && !match) {
		insert_preserved_val(p, cur_val, idx);
		return true;
	}

	UNUSED_PARAMETER(ppts);
	return false;
}

static bool refresh_list(obs_properties_t *props, obs_property_t *prop, obs_data_t *settings)
{
	(void)props;
	(void)settings;
	obs_property_list_clear(prop);

	auto plugs = update_plugs();
	std::sort(plugs.begin(), plugs.end());
	obs_property_list_add_string(prop, "", "");
	for (const auto &plug : plugs) {
		obs_property_list_add_string(prop, plug.monitor.c_str(), encodePlug(plug).c_str());
	}

	auto windows = update_windows();
	std::sort(windows.begin(), windows.end());
	for (const auto &wnd : windows) {
		obs_property_list_add_string(prop, wnd.title.c_str(), encodeWindow(wnd).c_str());
	}

	ms_check_window_property_setting(props, prop, settings, "source", 0);
	return true;
}

static obs_properties_t *mutter_capture_get_properties(void *data)
{
	(void)data;
	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop =
		obs_properties_add_list(props, "source", "Source", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(prop, "", "");

	obs_property_set_modified_callback(prop, refresh_list);

	obs_properties_add_bool(props, "cursor", "Draw mouse cursor");
	// obs_properties_add_bool(props, "timestamps", "Use running time as time stamps");

	return props;
}

static void mutter_capture_update(void *data, obs_data_t *settings)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(data);

	capture->cursor_visible = obs_data_get_bool(settings, "cursor");
	capture->config = obs_data_get_string(settings, "source");

#if 0
	if (capture->obs_pw_stream)
		obs_pipewire_stream_set_cursor_visible(capture->obs_pw_stream, capture->cursor_visible);
#else
	reload_session(capture);
#endif
}

static void mutter_capture_show(void *data)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(data);

	if (capture->obs_pw_stream)
		obs_pipewire_stream_show(capture->obs_pw_stream);
}

static void mutter_capture_hide(void *data)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(data);

	if (capture->obs_pw_stream)
		obs_pipewire_stream_hide(capture->obs_pw_stream);
}

static uint32_t mutter_capture_get_width(void *data)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(data);

	if (capture->obs_pw_stream)
		return obs_pipewire_stream_get_width(capture->obs_pw_stream);
	else
		return 0;
}

static uint32_t mutter_capture_get_height(void *data)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(data);

	if (capture->obs_pw_stream)
		return obs_pipewire_stream_get_height(capture->obs_pw_stream);
	else
		return 0;
}

static void mutter_capture_video_render(void *data, gs_effect_t *effect)
{
	struct mutter_capture *capture = reinterpret_cast<struct mutter_capture *>(data);

	if (capture->obs_pw_stream)
		obs_pipewire_stream_video_render(capture->obs_pw_stream, effect);
}

void mutter_capture_init(void)
{
	const struct obs_source_info mutter_capture_info = {
		.id = "gnome-mutter-screencast-source",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE,
		.get_name = mutter_capture_get_name,
		.create = mutter_capture_create,
		.destroy = mutter_capture_destroy,
		.get_width = mutter_capture_get_width,
		.get_height = mutter_capture_get_height,
		.get_defaults = mutter_capture_get_defaults,
		.get_properties = mutter_capture_get_properties,
		.update = mutter_capture_update,
		.show = mutter_capture_show,
		.hide = mutter_capture_hide,
		.video_render = mutter_capture_video_render,
		.icon_type = OBS_ICON_TYPE_DESKTOP_CAPTURE,
	};
	obs_register_source(&mutter_capture_info);
}

void mutter_capture_deinit(void)
{
	if (connection) {
		g_object_unref(connection);
		connection = NULL;
	}
}
