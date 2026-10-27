/*
 * obs-gnome-mutter-screencast. OBS Studio source plugin.
 * Copyright (C) 2019-2020 Florian Zwoch <fzwoch@gmail.com>
 *
 * This file is part of obs-gnome-mutter-screencast.
 *
 * obs-gnome-mutter-screencast is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * obs-gnome-mutter-screencast is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with obs-gnome-mutter-screencast. If not, see <http://www.gnu.org/licenses/>.
 */

#include "gnome-capture.h"

#include <obs/obs-module.h>
#include <obs/util/platform.h>
#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <gst/video/video.h>
#include <json-glib/json-glib.h>
#include <unistd.h>

#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>

typedef struct {
	std::string connector;
	std::string monitor;
} plug_t;

typedef struct {
	guint64 id;
	std::string title;
	std::string wm_class;
} window_t;

struct data_t {
	GstElement *pipe = NULL;
	obs_source_t *source = NULL;
	obs_data_t *settings = NULL;
	uint64_t count = 0;

	GMainLoop *loop = NULL;
	bool active;
	std::mutex mutex;
	std::condition_variable cv;
	std::thread thread;
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

static std::vector<plug_t> update_plug_names()
{
	std::vector<plug_t> plugs;

	GError *err = NULL;
	GVariant *display_config = NULL;

	GDBusConnection *dbus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
	if (err != NULL) {
		blog(LOG_ERROR, "Cannot connect to DBus: %s", err->message);
		g_error_free(err);

		goto fail;
	}

	display_config = g_dbus_connection_call_sync(dbus, "org.gnome.Mutter.DisplayConfig",
						     "/org/gnome/Mutter/DisplayConfig",
						     "org.gnome.Mutter.DisplayConfig", "GetCurrentState", NULL, NULL,
						     G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

	if (err != NULL) {
		blog(LOG_ERROR, "Cannot call GetCurrentState() on DBus: %s", err->message);
		g_error_free(err);

		goto fail;
	}

	GVariant *list;

	g_variant_get(display_config, "(u@a((ssss)a(siiddada{sv})a{sv})a(iiduba(ssss)a{sv})a{sv})", NULL, &list, NULL,
		      NULL);

	GVariantIter iter;
	g_variant_iter_init(&iter, list);

	gchar *connector;
	gchar *monitor;

	while (g_variant_iter_loop(&iter, "((ssss)a(siiddada{sv})a{sv})", &connector, NULL, &monitor, NULL, NULL,
				   NULL)) {
		plug_t plug;
		plug.connector = connector;
		plug.monitor = monitor;

		plugs.push_back(std::move(plug));
	}

	g_variant_unref(list);

fail:
	if (display_config != NULL)
		g_variant_unref(display_config);

	if (dbus != NULL)
		g_object_unref(dbus);

	return plugs;
}

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

static const char *get_name(void * /*type_data*/)
{
	return "GNOME Mutter Screen Cast";
}

static gboolean bus_callback(GstBus * /*bus*/, GstMessage *message, gpointer user_data)
{
	data_t *data = (data_t *)user_data;

	switch (GST_MESSAGE_TYPE(message)) {
	case GST_MESSAGE_EOS:
		obs_source_output_video(data->source, NULL);
		break;
	case GST_MESSAGE_ERROR: {
		GError *err;
		gst_message_parse_error(message, &err, NULL);
		blog(LOG_ERROR, "%s", err->message);
		g_error_free(err);
	}
		gst_element_set_state(data->pipe, GST_STATE_NULL);
		obs_source_output_video(data->source, NULL);
		break;
	default:
		break;
	}

	return TRUE;
}

static GstFlowReturn new_sample(GstAppSink *appsink, gpointer user_data)
{
	data_t *data = (data_t *)user_data;
	GstSample *sample = gst_app_sink_pull_sample(appsink);
	GstBuffer *buffer = gst_sample_get_buffer(sample);
	GstCaps *caps = gst_sample_get_caps(sample);
	GstVideoCropMeta *meta = gst_buffer_get_video_crop_meta(buffer);
	GstMapInfo info;
	GstVideoInfo video_info;

	gst_video_info_from_caps(&video_info, caps);
	gst_buffer_map(buffer, &info, GST_MAP_READ);

	// somehow we can end up with empty buffers?
	if (info.data == NULL) {
		gst_buffer_unmap(buffer, &info);
		gst_sample_unref(sample);

		return GST_FLOW_OK;
	}

	switch (video_info.finfo->format) {
	case GST_VIDEO_FORMAT_BGRx:
	case GST_VIDEO_FORMAT_BGRA:
		break;
	default:
		blog(LOG_ERROR, "Unexpected video format: %s", video_info.finfo->name);

		gst_buffer_unmap(buffer, &info);
		gst_sample_unref(sample);

		return GST_FLOW_OK;
	}

	struct obs_source_frame frame = {};

	frame.width = video_info.width;
	frame.height = video_info.height;
	frame.format = VIDEO_FORMAT_BGRA;
	frame.linesize[0] = video_info.stride[0];
	frame.data[0] = info.data + video_info.offset[0];

	frame.timestamp = obs_data_get_bool(data->settings, "timestamps") ? os_gettime_ns() : data->count++;

	if (meta != NULL) {
		frame.width = meta->width;
		frame.height = meta->height;
		frame.data[0] += meta->y * video_info.stride[0] + meta->x * 4;
	}

	enum video_range_type range = VIDEO_RANGE_DEFAULT;
	switch (video_info.colorimetry.range) {
	case GST_VIDEO_COLOR_RANGE_0_255:
		range = VIDEO_RANGE_FULL;
		frame.full_range = 1;
		break;
	case GST_VIDEO_COLOR_RANGE_16_235:
		range = VIDEO_RANGE_PARTIAL;
		break;
	default:
		break;
	}

	enum video_colorspace cs = VIDEO_CS_DEFAULT;
	switch (video_info.colorimetry.matrix) {
	case GST_VIDEO_COLOR_MATRIX_BT709:
		cs = VIDEO_CS_709;
		break;
	case GST_VIDEO_COLOR_MATRIX_BT601:
		cs = VIDEO_CS_601;
		break;
	default:
		break;
	}

	video_format_get_parameters(cs, range, frame.color_matrix, frame.color_range_min, frame.color_range_max);

	obs_source_output_video(data->source, &frame);

	gst_buffer_unmap(buffer, &info);
	gst_sample_unref(sample);

	return GST_FLOW_OK;
}

static void dbus_stream_closed_cb(GDBusConnection * /*connection*/, const gchar * /*sender_name*/,
				  const gchar * /*object_path*/, const gchar * /*interface_name*/,
				  const gchar * /*signal_name*/, GVariant * /*parameters*/, gpointer user_data)
{
	data_t *data = (data_t *)user_data;

	if (data->pipe != NULL) {
		gst_element_set_state(data->pipe, GST_STATE_NULL);

		GstBus *bus = gst_element_get_bus(data->pipe);
		gst_bus_remove_watch(bus);
		gst_object_unref(bus);

		gst_object_unref(data->pipe);
		data->pipe = NULL;

		{
			std::lock_guard<std::mutex> lck(data->mutex);
			if (data->loop)
				g_main_loop_quit(data->loop);
		}
	}

	obs_source_output_video(data->source, NULL);
}

static void dbus_cb(GDBusConnection * /*connection*/, const gchar * /*sender_name*/, const gchar * /*object_path*/,
		    const gchar * /*interface_name*/, const gchar * /*signal_name*/, GVariant *parameters,
		    gpointer user_data)
{
	GError *err = NULL;
	data_t *data = (data_t *)user_data;
	guint node_id = 0;

	g_variant_get(parameters, "(u)", &node_id, NULL);

	gchar *pipeline = g_strdup_printf("pipewiresrc client-name=obs-studio path=%u always-copy=true ! video/x-raw ! "
					  "appsink max-buffers=2 drop=true sync=false name=appsink",
					  node_id);

	data->pipe = gst_parse_launch(pipeline, &err);
	g_free(pipeline);

	if (err != NULL) {
		blog(LOG_ERROR, "Error gst_parse_launch(): %s", err->message);
		g_error_free(err);

		return;
	}

	GstAppSinkCallbacks appsink_cbs = {NULL, NULL, new_sample};

	GstElement *appsink = gst_bin_get_by_name(GST_BIN(data->pipe), "appsink");
	gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &appsink_cbs, data, NULL);
	gst_object_unref(appsink);

	GstBus *bus = gst_element_get_bus(data->pipe);
	gst_bus_add_watch(bus, bus_callback, data);
	gst_object_unref(bus);

	gst_element_set_state(data->pipe, GST_STATE_PLAYING);
}

static bool await_exit(data_t *data)
{
	std::unique_lock<std::mutex> lck(data->mutex);
	if (!data->active)
		return false;

	data->cv.wait_for(lck, std::chrono::seconds(2));
	return data->active;
}

static void _start(data_t *data)
{
retry:
	GError *err = NULL;
	GVariant *stream_res = NULL;
	GVariant *session_res = NULL;
	std::optional<guint> subscribe_id;
	const char *src;
	GstBus *bus;
	std::string a_session_path;

	data->count = 0;

	GMainContext *context = g_main_context_new();

	g_main_context_push_thread_default(context);

	GDBusConnection *dbus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
	if (err != NULL) {
		blog(LOG_ERROR, "Cannot connect to DBus: %s", err->message);
		g_error_free(err);
		err = NULL;

		goto fail2;
	}

	session_res = g_dbus_connection_call_sync(dbus, "org.gnome.Mutter.ScreenCast", "/org/gnome/Mutter/ScreenCast",
						  "org.gnome.Mutter.ScreenCast", "CreateSession",
						  g_variant_new_parsed("({'dummy' : <0>},)"), NULL,
						  G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

	if (err != NULL) {
		blog(LOG_ERROR, "Cannot call CreateSession() on DBus: %s", err->message);
		g_error_free(err);
		err = NULL;

		goto fail;
	}

	gchar *session_path;
	g_variant_get(session_res, "(o)", &session_path, NULL);

	a_session_path = g_strdup(session_path);

	do {
		src = obs_data_get_string(data->settings, "source");
		if (!src) {
			continue;
		}

		{
			auto decodedWnd = decode(src);
			if (auto plug = std::get_if<PlugDecode>(&decodedWnd)) {
				stream_res = g_dbus_connection_call_sync(
					dbus, "org.gnome.Mutter.ScreenCast", session_path,
					"org.gnome.Mutter.ScreenCast.Session", "RecordMonitor",
					g_variant_new_parsed("(%s, {'cursor-mode' : <%u>})", plug->connector.c_str(),
							     obs_data_get_bool(data->settings, "cursor") ? 1 : 0),
					NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
			} else if (auto wnd = std::get_if<WindowDecode>(&decodedWnd)) {
				std::optional<uint64_t> window_id = find_window_id(wnd->wm_class, wnd->id);
				if (!window_id)
					continue;

				stream_res = g_dbus_connection_call_sync(
					dbus, "org.gnome.Mutter.ScreenCast", session_path,
					"org.gnome.Mutter.ScreenCast.Session", "RecordWindow",
					g_variant_new_parsed("({'window-id' : <%t>, 'cursor-mode' : <%u>},)",
							     *window_id,
							     obs_data_get_bool(data->settings, "cursor") ? 1 : 0),
					NULL, G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);
			} else {
				continue;
			}
		}

		if (err != NULL) {
			g_error_free(err);
			err = NULL;
			continue;
		} else {
			break;
		}
	} while (await_exit(data));

	{
		std::lock_guard<std::mutex> lck(data->mutex);
		if (!data->active)
			goto fail;
	}

	gchar *stream_path;
	g_variant_get(stream_res, "(o)", &stream_path, NULL);

	subscribe_id = g_dbus_connection_signal_subscribe(dbus, NULL, NULL, "Closed", session_path, NULL,
							  (GDBusSignalFlags)G_DBUS_CALL_FLAGS_NONE,
							  dbus_stream_closed_cb, data, NULL);

	g_dbus_connection_signal_subscribe(dbus, NULL, NULL, "PipeWireStreamAdded", stream_path, NULL,
					   (GDBusSignalFlags)G_DBUS_CALL_FLAGS_NONE, dbus_cb, data, NULL);

	g_dbus_connection_call_sync(dbus, "org.gnome.Mutter.ScreenCast", session_path,
				    "org.gnome.Mutter.ScreenCast.Session", "Start", NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
				    -1, NULL, &err);

	if (err != NULL) {
		blog(LOG_ERROR, "Cannot call Start() on DBus: %s", err->message);
		g_error_free(err);
		err = NULL;

		goto fail;
	}

fail:
	if (session_res != NULL)
		g_variant_unref(session_res);

	if (stream_res != NULL)
		g_variant_unref(stream_res);

	if (dbus != NULL)
		g_object_unref(dbus);

	{
		std::lock_guard<std::mutex> lck(data->mutex);
		if (!data->active)
			goto fail2;

		data->loop = g_main_loop_new(context, FALSE);
	}

	g_main_loop_run(data->loop);

	if (data->pipe == NULL)
		goto fail2;

	gst_element_set_state(data->pipe, GST_STATE_NULL);

	bus = gst_element_get_bus(data->pipe);
	gst_bus_remove_watch(bus);
	gst_object_unref(bus);

	gst_object_unref(data->pipe);
	data->pipe = NULL;

	dbus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
	if (err != NULL) {
		blog(LOG_ERROR, "Cannot connect to DBus: %s", err->message);
		g_error_free(err);
		err = NULL;

		goto fail2;
	}

fail2:
	if (subscribe_id) {
		g_dbus_connection_signal_unsubscribe(dbus, *subscribe_id);
		g_dbus_connection_call_sync(dbus, "org.gnome.Mutter.ScreenCast", a_session_path.c_str(),
					    "org.gnome.Mutter.ScreenCast.Session", "Stop", NULL, NULL,
					    G_DBUS_CALL_FLAGS_NONE, -1, NULL, &err);

		if (err != NULL) {
			blog(LOG_ERROR, "Cannot call Stop() on DBus: %s", err->message);
			g_error_free(err);
			err = NULL;
			err = NULL;
		}
	}

	if (dbus != NULL)
		g_object_unref(dbus);

	{
		std::lock_guard<std::mutex> lck(data->mutex);
		g_main_loop_unref(data->loop);
		data->loop = NULL;
	}

	g_main_context_unref(context);

	{
		std::lock_guard<std::mutex> lck(data->mutex);
		if (!data->active)
			return;
	}

	goto retry;
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	data_t *data = new data_t;

	data->source = source;
	data->settings = settings;

	return data;
}

static void start(data_t *data)
{
	data->active = true;
	data->thread = std::thread{_start, data};
}

static void stop(data_t *data)
{
	if (!data->thread.joinable()) {
		return;
	}

	{
		std::lock_guard<std::mutex> lck(data->mutex);
		if (data->loop)
			g_main_loop_quit(data->loop);

		data->active = false;
		data->cv.notify_one();
	}

	data->thread.join();

	obs_source_output_video(data->source, NULL);
}

static void destroy(void *p)
{
	data_t *data = (data_t *)p;

	stop(data);

	delete data;
}

static void get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "source", "");
	obs_data_set_default_bool(settings, "cursor", true);
	obs_data_set_default_bool(settings, "timestamps", false);
}

static obs_properties_t *get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop =
		obs_properties_add_list(props, "source", "Source", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	auto plugs = update_plug_names();
	obs_property_list_add_string(prop, "", "");
	for (const auto &plug : plugs) {
		obs_property_list_add_string(prop, plug.monitor.c_str(), encodePlug(plug).c_str());
	}

	auto windows = update_windows();
	for (const auto &wnd : windows) {
		obs_property_list_add_string(prop, wnd.title.c_str(), encodeWindow(wnd).c_str());
	}

	obs_properties_add_bool(props, "cursor", "Draw mouse cursor");
	obs_properties_add_bool(props, "timestamps", "Use running time as time stamps");

	return props;
}

static void update(void *p, obs_data_t * /*settings*/)
{
	data_t *data = (data_t *)p;
	{
		std::lock_guard<std::mutex> lck(data->mutex);
		if (!data->loop)
			return;
	}

	// mind that we can technically race with data->loop being destroyed, in this case it is just a noop
	stop(data);
	start(data);
}

static void show(void *data)
{
	start((data_t *)data);
}

static void hide(void *data)
{
	stop((data_t *)data);
}

void gnome_capture_init()
{
	struct obs_source_info info = {
		.id = "gnome-mutter-screencast-source",
		.type = OBS_SOURCE_TYPE_INPUT,
		.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE,

		.get_name = get_name,
		.create = create,
		.destroy = destroy,

		.get_defaults = get_defaults,
		.get_properties = get_properties,
		.update = update,
		.show = show,
		.hide = hide,
	};

	obs_register_source(&info);

	gst_init(NULL, NULL);
}
