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
	"1.0",
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

typedef struct diff_cmd {
	gchar *cmd;    /* Command line with replaceable wildcards */
	gchar *label;  /* Label to show in settings panel */
	gboolean sync; /* Whether to run command synchronously */
} diff_cmd;

static const diff_cmd stock_commands[] = {
	{
		"diff -u \"%fc\" \"%ft\"",
		"diff",
		TRUE },
	{
		"diff \"%fc\" \"%ft\"",
		"diff (plain)",
		TRUE },
	{
		"meld \"%fc\" \"%ft\"",
		"Meld",
		FALSE },
	{
		"kompare \"%fc\" \"%ft\"",
		"Kompare",
		FALSE },
	{
		"opendiff \"%fc\" \"%ft\"",
		"FileMerge",
		FALSE }
};

static const gint cmd_count = sizeof(stock_commands)/sizeof(diff_cmd);

static void get_current_cmd(diff_cmd *dest)
{
	if (config_cmd_index == cmd_count) {
		dest->cmd  = config_custom_cmd;
		dest->sync = config_stdout_en;
	}
	else
		*dest = stock_commands[config_cmd_index];
}

static gchar *export_doc_to_tmpfile(GeanyDocument *doc, GError **error)
{
	ScintillaObject *sci = doc->editor->sci;
	gchar *doc_text = sci_get_contents(sci, -1);
	gchar *filename;

	if (g_file_open_tmp(NULL, &filename, error) == -1)
		return NULL;

	if (!g_file_set_contents(filename, doc_text, -1, error))
		return NULL;

	return filename;
}

static void on_doc_menu_item_clicked(gpointer item, GeanyDocument *doc)
{
	GeanyFiletype *ft = filetypes_lookup_by_name("Diff");
	GeanyDocument *cur_doc;
	diff_cmd cur_cmd;
	GError *error = NULL;
	GString *command;
	gchar *std_out;
	gchar *file_tgt;
	gboolean itself;

	if (doc->is_valid) {
		cur_doc = document_get_current();
		get_current_cmd(&cur_cmd);

		itself = !g_strcmp0(cur_doc->file_name, doc->file_name);

		/* When comparing to itself, get unsaved version of doc in a tmp file */
		if (itself) {
			file_tgt = export_doc_to_tmpfile(doc, &error);
			if (!file_tgt) {
				g_warning("geanydiff: error: %s", error->message);
				ui_set_statusbar(FALSE, _("geanydiff: error: %s"), error->message);
				g_error_free(error);
				return;
			}
		}
		else
			file_tgt = doc->file_name;

		command = g_string_new(cur_cmd.cmd);

		utils_string_replace_all(command, "%fc", cur_doc->file_name);
		utils_string_replace_all(command, "%ft", file_tgt);

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

		if (itself)
			g_free(file_tgt);

		g_string_free(command, TRUE);
	}
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

void plugin_init(GeanyData *data)
{
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
		  "%ft will be replaced with the full path of target document"));
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
		  "\tDiff is always performed with last saved version of documents,\n"
		  "\texcept when targeting the same document as current one,\n"
		  "\twhich will compare to unsaved content."));
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

	if (tool_item != NULL) {
		gtk_widget_destroy(GTK_WIDGET(tool_item));
		tool_item = NULL;
	}
	if (menu_item != NULL) {
		gtk_widget_destroy(menu_item);
		menu_item = NULL;
	}
}

