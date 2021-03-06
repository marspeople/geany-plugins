#include <geanyplugin.h>

#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

GeanyPlugin     *geany_plugin;
GeanyData       *geany_data;
GeanyFunctions  *geany_functions;

PLUGIN_VERSION_CHECK(211)

PLUGIN_SET_TRANSLATABLE_INFO(
	LOCALEDIR,
	GETTEXT_PACKAGE,
	_("Diff"),
	_("Invoke a diff tool between two files"),
	"1.2",
	"Marcelo Póvoa <marspeoplester@gmail.com>")

static GtkToolItem *tool_item       = NULL;
static GtkWidget *menu_item         = NULL;
static GtkWidget *pref_cmd_combo    = NULL;
static GtkWidget *pref_custom_label = NULL;
static GtkWidget *pref_custom_cmd   = NULL;
static GtkWidget *pref_stdout_en    = NULL;

/* User settings at runtime */
static gint config_cmd_index;
static gchar *config_custom_cmd;
static gboolean config_stdout_en;

static gchar *config_file;

enum {
  KB_DIFF_LEFT,
  KB_DIFF_RIGHT,
  KB_DIFF_SELF,
  KB_COUNT
};

typedef struct diff_cmd {
	const gchar * cmd;    /* Command line with replaceable wildcards */
	const gchar * label;  /* Label to show in settings panel */
	gboolean sync;        /* Whether to run command synchronously */
} diff_cmd;

static const diff_cmd stock_commands[] = {
	{
		"diff -u \"%ft\" \"%fc\"",
		"diff",
		TRUE },
	{
		"diff \"%ft\" \"%fc\"",
		"diff (plain)",
		TRUE },
	{
		"opendiff \"%ft\" \"%fc\"",
		"FileMerge",
		FALSE },
	{
		"kdiff3 \"%ft\" \"%fc\"",
		"KDiff3",
		FALSE },
	{
		"kompare \"%ft\" \"%fc\"",
		"Kompare",
		FALSE },
	{
		"meld \"%ft\" \"%fc\"",
		"Meld",
		FALSE },
	{
		"tkdiff \"%ft\" \"%fc\"",
		"TkDiff",
		FALSE }
};

static const gint cmd_count = sizeof(stock_commands)/sizeof(diff_cmd);

static void get_current_cmd(diff_cmd *dest)
{
	/* Last index is the custom command by definition */
	if (config_cmd_index == cmd_count) {
		dest->cmd  = config_custom_cmd;
		dest->sync = config_stdout_en;
	}
	else *dest = stock_commands[config_cmd_index];
}

static gchar *export_doc_to_tmpfile(GeanyDocument *doc, GError **error)
{
	/* FIXME: File encoding doesn't seem to be correctly preserved */
	ScintillaObject *sci = doc->editor->sci;
	gchar *doc_text = sci_get_contents(sci, -1);
	gchar *basename, *template, *name_used;

	basename = g_path_get_basename(DOC_FILENAME(doc));
	template = g_strconcat(basename, "-XXXXXX", NULL);
	close(g_file_open_tmp(template, &name_used, error));

	if (!g_file_set_contents(name_used, doc_text, -1, error))
		return NULL;

	g_free(doc_text);
	g_free(basename);
	g_free(template);

	return name_used;
}

static gchar *get_latest_file_snapshot(GeanyDocument *doc, GError *error)
{
	/* If document is not saved, get unsaved version of doc in a tmp file */
	if (doc->changed || !doc->real_path) {
		return export_doc_to_tmpfile(doc, &error);
	}
	return doc->file_name;
}

static void process_diff_request(GeanyDocument *tgt_doc)
{
	GeanyFiletype *ft = filetypes_lookup_by_name("Diff");
	GeanyDocument *cur_doc = document_get_current();
	diff_cmd cur_cmd;
	GError *error = NULL;
	GString *command;
	gchar *std_out;
	gchar *file_cur;
	gchar *file_tgt;

	if (DOC_VALID(cur_doc) && DOC_VALID(tgt_doc)) {
		get_current_cmd(&cur_cmd);

		file_cur = get_latest_file_snapshot(cur_doc, error);
		if (cur_doc == tgt_doc) {
			if (!cur_doc->real_path) return;
			file_tgt = tgt_doc->file_name;
		} else {
			file_tgt = get_latest_file_snapshot(tgt_doc, error);
		}

		if (error) {
			g_warning("geanydiff: error: %s", error->message);
			ui_set_statusbar(FALSE, _("geanydiff: error: %s"), error->message);
			g_error_free(error);
		}

		command = g_string_new(cur_cmd.cmd);

		utils_string_replace_all(command, "%ft", file_tgt);
		utils_string_replace_all(command, "%fc", file_cur);

		if (cur_cmd.sync)
			g_spawn_command_line_sync(command->str, &std_out, NULL, NULL, &error);
		else
			g_spawn_command_line_async(command->str, &error);

		if (error) {
			g_warning("geanydiff: error: %s", error->message);
			ui_set_statusbar(FALSE, _("geanydiff: error: %s"), error->message);
			g_error_free(error);
		}

		/* Copy command stdout to a new "diff" document */
		if (cur_cmd.sync) {
			document_new_file("diff", ft, std_out);
			g_free(std_out);
		}

		if (g_strcmp0(file_cur, cur_doc->file_name)) g_free(file_cur);
		if (g_strcmp0(file_tgt, tgt_doc->file_name)) g_free(file_tgt);

		g_string_free(command, TRUE);
	}
}

static void on_doc_menu_item_clicked(gpointer item, GeanyDocument *doc)
{
	process_diff_request(doc);
}

static void on_doc_menu_show(GtkMenu *menu)
{
	/* clear the old menu items */
	gtk_container_foreach(GTK_CONTAINER(menu), (GtkCallback) gtk_widget_destroy, NULL);

	ui_menu_add_document_items(menu, document_get_current(),
		G_CALLBACK(on_doc_menu_item_clicked));
}

static void config_load(void)
{
	GKeyFile *config = g_key_file_new();

	config_file = g_strconcat(geany->app->configdir, G_DIR_SEPARATOR_S, "plugins", G_DIR_SEPARATOR_S,
		"geanydiff", G_DIR_SEPARATOR_S, "geanydiff.conf", NULL);
	g_key_file_load_from_file(config, config_file, G_KEY_FILE_NONE, NULL);

	config_custom_cmd = utils_get_setting_string(config, "geanydiff", "custom_cmd", "");
	config_cmd_index  = utils_get_setting_integer(config, "geanydiff", "cmd_index", 0);
	config_stdout_en  = utils_get_setting_boolean(config, "geanydiff", "stdout_en", FALSE);

	g_key_file_free(config);
}

static void config_save(void)
{
	GKeyFile *config = g_key_file_new();
	gchar *data;
	gchar *config_dir = g_path_get_dirname(config_file);

	g_key_file_load_from_file(config, config_file, G_KEY_FILE_NONE, NULL);

	g_key_file_set_string(config, "geanydiff", "custom_cmd", config_custom_cmd);
	g_key_file_set_integer(config, "geanydiff", "cmd_index", config_cmd_index);
	g_key_file_set_boolean(config, "geanydiff", "stdout_en", config_stdout_en);

	if (! g_file_test(config_dir, G_FILE_TEST_IS_DIR) && utils_mkdir(config_dir, TRUE) != 0) {
		dialogs_show_msgbox(GTK_MESSAGE_ERROR,
			_("Plugin configuration directory could not be created."));
	}
	else {
		/* write config to file */
		data = g_key_file_to_data(config, NULL, NULL);
		utils_write_file(config_file, data);
		g_free(data);
	}
	g_free(config_dir);
	g_key_file_free(config);
}

static void on_click_tool_button(void)
{
	plugin_show_configure(geany_plugin);
}

static void on_kb_diff_left(guint key_id)
{
	GeanyDocument* cur_doc = document_get_current();
	if (cur_doc) {
		gint notebook_page = document_get_notebook_page(cur_doc);
		process_diff_request(document_get_from_page(notebook_page - 1));
	}
}

static void on_kb_diff_right(guint key_id)
{
  GeanyDocument* cur_doc = document_get_current();
  if (cur_doc) {
		gint notebook_page = document_get_notebook_page(cur_doc);
		process_diff_request(document_get_from_page(notebook_page + 1));
	}
}

static void on_kb_diff_self(guint key_id)
{
	process_diff_request(document_get_current());
}

void plugin_init(GeanyData *data)
{
	GeanyKeyGroup *group;

  group = plugin_set_key_group(geany_plugin, "geanydiff", KB_COUNT, NULL);
  keybindings_set_item(group, KB_DIFF_LEFT, on_kb_diff_left,
                        0, 0, "diff_left", _("Diff to Left Tab"), NULL);
  keybindings_set_item(group, KB_DIFF_RIGHT, on_kb_diff_right,
                        0, 0, "diff_right", _("Diff to Right Tab"), NULL);
  keybindings_set_item(group, KB_DIFF_SELF, on_kb_diff_self,
                        0, 0, "diff_self", _("Diff to Saved File"), NULL);

	tool_item = gtk_menu_tool_button_new(NULL, NULL);
	gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(tool_item), GTK_STOCK_DND_MULTIPLE);
	plugin_add_toolbar_item(geany_plugin, tool_item);
	g_signal_connect(tool_item, "clicked", G_CALLBACK(on_click_tool_button), NULL);
	gtk_widget_show_all(GTK_WIDGET(tool_item));

	menu_item = gtk_menu_new();
	gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(tool_item), menu_item);
	g_signal_connect(menu_item, "show", G_CALLBACK(on_doc_menu_show), NULL);

	config_load();
}

static void on_configure_response(GtkDialog *dialog, gint response, gpointer user_data)
{
	if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY) {
		g_free(config_custom_cmd);
		config_cmd_index  = gtk_combo_box_get_active((GtkComboBox*) pref_cmd_combo);
		config_custom_cmd = g_strdup(gtk_entry_get_text(GTK_ENTRY(pref_custom_cmd)));
		config_stdout_en  = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pref_stdout_en));
	}
}

static void on_changed_command(void)
{
	gboolean custom = (gtk_combo_box_get_active((GtkComboBox*) pref_cmd_combo) == cmd_count);

	gtk_widget_set_sensitive(pref_custom_cmd, custom);
	gtk_widget_set_sensitive(pref_custom_label, custom);
	gtk_widget_set_sensitive(pref_stdout_en, custom);
}

GtkWidget *plugin_configure(GtkDialog *dialog)
{
	GtkWidget *vbox, *gtkw;
	gint i;

	vbox = gtk_vbox_new(FALSE, 6);

	/* Combobox label */
	gtkw = gtk_label_new(_("Diff command:"));
	gtk_misc_set_alignment(GTK_MISC(gtkw), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), gtkw, FALSE, FALSE, 0);

	/* Combobox with stock diff tools */
	gtkw = gtk_combo_box_new_text();
	for (i = 0; i < cmd_count; i++)
		gtk_combo_box_append_text((GtkComboBox*) gtkw, _(stock_commands[i].label));

	/* Add extra entry for custom command */
	gtk_combo_box_append_text((GtkComboBox*) gtkw, _("Custom..."));

	gtk_combo_box_set_active((GtkComboBox*) gtkw, config_cmd_index);
	gtk_box_pack_start(GTK_BOX(vbox), gtkw, FALSE, FALSE, 2);
	g_object_set_data(G_OBJECT(dialog), "geanydiff_commands", gtkw);
	g_signal_connect(gtkw, "changed", on_changed_command, NULL);
	pref_cmd_combo = gtkw;

	/* Input label */
	gtkw = gtk_label_new(_("Custom command:"));
	gtk_misc_set_alignment(GTK_MISC(gtkw), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), gtkw, FALSE, FALSE, 0);
	pref_custom_label = gtkw;

	/* Input for custom diff command */
	gtkw = gtk_entry_new();
	if (config_custom_cmd != NULL)
		gtk_entry_set_text(GTK_ENTRY(gtkw), config_custom_cmd);

	gtk_widget_set_tooltip_text(gtkw,
		_("If non-empty, the custom diff command line to execute.\n"
		  "%fc will be replaced with the full path of current document\n"
		  "%ft will be replaced with the full path of target (chosen) document"));
	gtk_box_pack_start(GTK_BOX(vbox), gtkw, FALSE, FALSE, 0);
	pref_custom_cmd = gtkw;

	/* Checkbox for stdout based diff */
	gtkw = gtk_check_button_new_with_label(_("Capture standard output"));
	gtk_widget_set_tooltip_text(gtkw,
		_("Enable if custom command is text-based (i.e, diff is\n"
		  "given in stdout) rather having a GUI. Enabling this\n"
		  "option will freeze Geany until command exits.\n"));
	gtk_button_set_focus_on_click(GTK_BUTTON(gtkw), FALSE);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(gtkw), config_stdout_en);
	gtk_box_pack_start(GTK_BOX(vbox), gtkw, FALSE, FALSE, 0);
	pref_stdout_en = gtkw;

	gtkw = gtk_label_new(
		_("\nNote:\n"
		  "\tDiff will compare temporary snapshots of unsaved files, except\n"
		  "\twhen requesting diff to itself (will compare to the saved copy).\n"));
	gtk_misc_set_alignment(GTK_MISC(gtkw), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(vbox), gtkw, FALSE, FALSE, 0);

	gtk_widget_show_all(vbox);
	on_changed_command();

	g_signal_connect(dialog, "response", G_CALLBACK(on_configure_response), NULL);
	return vbox;
}

void plugin_cleanup(void)
{
	config_save();
	g_free(config_file);
	g_free(config_custom_cmd);

	if (menu_item != NULL) {
		gtk_widget_destroy(menu_item);
		menu_item = NULL;
	}
	if (tool_item != NULL) {
		gtk_widget_destroy(GTK_WIDGET(tool_item));
		tool_item = NULL;
	}
}
