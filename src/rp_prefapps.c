#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include <X11/Xlib.h>

#include <libintl.h>

/* Controls */

static GtkWidget *main_dlg, *msg_dlg, *msg_msg, *msg_pb, *msg_btn;;
static GtkWidget *cat_tv, *pack_tv;
static GtkWidget *cancel_btn, *install_btn;

GtkListStore *categories, *packages;

guint inst, uninst;
gchar **pnames, **pinst, **puninst;

static void category_selected (GtkTreeView *tv, gpointer ptr);


static gboolean ok_clicked (GtkButton *button, gpointer data)
{
    gtk_widget_destroy (GTK_WIDGET (msg_dlg));
    msg_dlg = NULL;
    return FALSE;
}

static void message (char *msg, int wait, int prog)
{
    if (!msg_dlg)
    {
        GtkBuilder *builder;
        GtkWidget *wid;
        GdkColor col;

        builder = gtk_builder_new ();
        gtk_builder_add_from_file (builder, PACKAGE_DATA_DIR "/rp_prefapps.ui", NULL);

        msg_dlg = (GtkWidget *) gtk_builder_get_object (builder, "msg");
        gtk_window_set_modal (GTK_WINDOW (msg_dlg), TRUE);
        gtk_window_set_transient_for (GTK_WINDOW (msg_dlg), GTK_WINDOW (main_dlg));
        gtk_window_set_position (GTK_WINDOW (msg_dlg), GTK_WIN_POS_CENTER_ON_PARENT);
        gtk_window_set_destroy_with_parent (GTK_WINDOW (msg_dlg), TRUE);

        wid = (GtkWidget *) gtk_builder_get_object (builder, "msg_eb");
        gdk_color_parse ("#FFFFFF", &col);
        gtk_widget_modify_bg (wid, GTK_STATE_NORMAL, &col);

        msg_msg = (GtkWidget *) gtk_builder_get_object (builder, "msg_lbl");
        msg_pb = (GtkWidget *) gtk_builder_get_object (builder, "msg_pb");
        msg_btn = (GtkWidget *) gtk_builder_get_object (builder, "msg_btn");

        gtk_label_set_text (GTK_LABEL (msg_msg), msg);

        gtk_widget_show_all (msg_dlg);
        g_object_unref (builder);
    }
    else gtk_label_set_text (GTK_LABEL (msg_msg), msg);

    if (wait)
    {
        g_signal_connect (msg_btn, "clicked", G_CALLBACK (ok_clicked), NULL);
        gtk_widget_set_visible (msg_pb, FALSE);
        gtk_widget_set_visible (msg_btn, TRUE);
    }
    else
    {
        gtk_widget_set_visible (msg_btn, FALSE);
        gtk_widget_set_visible (msg_pb, TRUE);
        if (prog == -1) gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
        else
        {
            float progress = prog / 100.0;
            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (msg_pb), progress);
        }
    }
}

static char* name_from_id (const gchar *id)
{
    GtkTreeIter iter;
    gboolean valid;
    gchar *tid, *name;

    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
    while (valid)
    {
        gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, 5, &tid, 6, &name, -1);
        if (!g_strcmp0 (id, tid)) return name;
        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
    }
    return NULL;
}


static void progress (PkProgress *progress, PkProgressType *type, gpointer data)
{
    char *buf, *name;
    int role = pk_progress_get_role (progress);
    int status = pk_progress_get_status (progress);

    //printf ("progress %d %d %d %d %s\n", role, type, status, pk_progress_get_percentage (progress), pk_progress_get_package_id (progress));

    if (msg_dlg)
    {
        switch (role)
        {
            case PK_ROLE_ENUM_REFRESH_CACHE :       if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Updating package data - please wait..."), 0, pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_RESOLVE :             if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Reading package status - please wait..."), 0, pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_INSTALL_PACKAGES :    if (status == PK_STATUS_ENUM_DOWNLOAD || status == PK_STATUS_ENUM_INSTALL)
                                                    {
                                                        name = name_from_id (pk_progress_get_package_id (progress));
                                                        if (name)
                                                        {
                                                            buf = g_strdup_printf (_("Installing %s - please wait..."), name);
                                                            message (buf, 0, pk_progress_get_percentage (progress));
                                                        }
                                                        else
                                                            message (_("Installing packages - please wait..."), 0, pk_progress_get_percentage (progress));
                                                    }
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_REMOVE_PACKAGES :     if (status == PK_STATUS_ENUM_REMOVE)
                                                    {
                                                        name = name_from_id (pk_progress_get_package_id (progress));
                                                        if (name)
                                                        {
                                                            buf = g_strdup_printf (_("Removing %s - please wait..."), name);
                                                            message (buf, 0, pk_progress_get_percentage (progress));
                                                        }
                                                        else
                                                            message (_("Removing packages - please wait..."), 0, pk_progress_get_percentage (progress));
                                                   }
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;
        }
    }
}


static void resolve_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    PkResults *results;
    PkError *pkerror;
    PkPackage *item;
    PkInfoEnum info;
    GError *error = NULL;
    GPtrArray *array;
    GtkTreeIter iter;
    gboolean valid;
    gchar *buf, *package_id, *summary;
    int i;

    results = pk_task_generic_finish (task, res, &error);
    if (error != NULL)
    {
        buf = g_strdup_printf (_("Error reading package status - %s"), error->message);
        message (buf, 1, -1);
        g_free (buf);
        return;
    }

    pkerror = pk_results_get_error_code (results);
    if (pkerror != NULL)
    {
        buf = g_strdup_printf (_("Error reading package status - %s"), pk_error_get_details (pkerror));
        message (buf, 1, -1);
        g_free (buf);
        return;
    }

    array = pk_results_get_package_array (results);

    for (i = 0; i < array->len; i++)
    {
        item = g_ptr_array_index (array, i);
        g_object_get (item, "info", &info, "package-id", &package_id, "summary", &summary,  NULL);

        valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
        while (valid)
        {
            gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, 4, &buf, -1);
            if (!strncmp (buf, package_id, strlen (buf)))
            {
                gtk_list_store_set (packages, &iter, 2, strstr (package_id, ";installed:") ? TRUE : FALSE, 5, package_id, -1);
                break;
            }
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
        }
    }

    // data now all loaded - show the main window
    gtk_tree_view_set_model (GTK_TREE_VIEW (cat_tv), GTK_TREE_MODEL (categories));
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL (categories), &iter);
    gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (cat_tv)), &iter);
    category_selected (GTK_TREE_VIEW (cat_tv), NULL);

    gtk_widget_destroy (GTK_WIDGET (msg_dlg));
    msg_dlg = NULL;
}

static void refresh_cache_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    PkResults *results;
    PkError *pkerror;
    GError *error = NULL;
    gchar *buf;

    results = pk_task_generic_finish (task, res, &error);
    if (error != NULL)
    {
        buf = g_strdup_printf (_("Error updating package data - %s"), error->message);
        message (buf, 1, -1);
        g_free (buf);
        return;
    }

    pkerror = pk_results_get_error_code (results);
    if (pkerror != NULL)
    {
        buf = g_strdup_printf (_("Error updating package data - %s"), pk_error_get_details (pkerror));
        message (buf, 1, -1);
        g_free (buf);
        return;
    }

    message (_("Reading package status - please wait..."), 0 , -1);

    pk_client_resolve_async (PK_CLIENT (task), 0, pnames, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) resolve_done, NULL);
}

static const char *cat_icon_name (char *category)
{
    if (!g_strcmp0 (category, "Programming"))
        return "applications-development";

    if (!g_strcmp0 (category, "Office"))
        return "applications-office";
}

static gboolean read_data_file (gpointer data)
{
    GtkTreeIter entry, cat_entry;
    GdkPixbuf *icon;
    GKeyFile *kf;
    PkTask *task;
    gchar **groups;
    gchar *buf, *cat, *name, *desc, *size, *iname;
    gboolean new;
    gchar *pname;
    int pcount = 0;

    kf = g_key_file_new ();
    if (g_key_file_load_from_file (kf, PACKAGE_DATA_DIR "/prefapps.conf", G_KEY_FILE_NONE, NULL))
    {
        gtk_list_store_append (GTK_LIST_STORE (categories), &cat_entry);
        icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "rpi", 32, 0, NULL);
        gtk_list_store_set (categories, &cat_entry, 0, icon, 1, "All Programs", -1);
        groups = g_key_file_get_groups (kf, NULL);

        while (*groups)
        {
            cat = g_key_file_get_value (kf, *groups, "category", NULL);
            name = g_key_file_get_value (kf, *groups, "name", NULL);
            desc = g_key_file_get_value (kf, *groups, "description", NULL);
            size = g_key_file_get_value (kf, *groups, "size", NULL);
            iname = g_key_file_get_value (kf, *groups, "icon", NULL);

            // create array of package names
            pnames = realloc (pnames, (pcount + 2) * sizeof (gchar *));
            pnames[pcount] = g_key_file_get_value (kf, *groups, "package", NULL);
            pcount++;
            pnames[pcount] = NULL;

            // add unique entries to category list
            new = TRUE;
            gtk_tree_model_get_iter_first (GTK_TREE_MODEL (categories), &cat_entry);
            while (gtk_tree_model_iter_next (GTK_TREE_MODEL (categories), &cat_entry))
            {
                gtk_tree_model_get (GTK_TREE_MODEL (categories), &cat_entry, 1, &buf, -1);
                if (!g_strcmp0 (cat, buf))
                {
                    new = FALSE;
                    g_free (buf);
                    break;
                }
                g_free (buf);
            } 

            if (new)
            {
                icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), cat_icon_name (cat), 32, 0, NULL);
                gtk_list_store_append (categories, &cat_entry);
                gtk_list_store_set (categories, &cat_entry, 0, icon, 1, cat, -1);
            }

            // create the entry for the packages list
            buf = g_strdup_printf ("<b>%s</b>\n%s\nSize : %s MB", name, desc, size);
            icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), iname, 32, 0, NULL);
            gtk_list_store_append (packages, &entry);
            gtk_list_store_set (packages, &entry, 0, icon, 1, buf, 2, FALSE, 3, cat, 4, pnames[pcount - 1], 5, "none", 6, name, -1);

            g_free (buf);
            g_object_unref (icon);
            g_free (cat);
            g_free (name);
            g_free (desc);
            g_free (size);
            g_free (iname);

            groups++;
        }
    }
    else
    {
        // handle no data file here...
        message (_("Unable to open package data file"), 1 , -1);
        return FALSE;
    }

    message (_("Updating package data - please wait..."), 0 , -1);

    task = pk_task_new ();
    pk_client_refresh_cache_async (PK_CLIENT (task), TRUE, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) refresh_cache_done, NULL);
    return FALSE;
}

static gboolean match_category (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    char *str;
    gboolean res;

    gtk_tree_model_get (model, iter, 5, &str, -1);
    if (!g_strcmp0 (str, "none")) res = FALSE;
    else
    {
        g_free (str);
        if (!g_strcmp0 ("All Programs", (char *) data)) return TRUE;
        gtk_tree_model_get (model, iter, 3, &str, -1);
        if (!g_strcmp0 (str, (char *) data)) res = TRUE;
        else res = FALSE;
    }
    g_free (str);
    return res;
}

static void category_selected (GtkTreeView *tv, gpointer ptr)
{
    GtkTreeModel *model, *fpackages;
    GtkTreeIter iter;
    GtkTreeSelection *sel;
    char *cat;

    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
    if (sel && gtk_tree_selection_get_selected (sel, &model, &iter))
    {
        gtk_tree_model_get (model, &iter, 1, &cat, -1);
        fpackages = gtk_tree_model_filter_new (GTK_TREE_MODEL (packages), NULL);
        gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (fpackages), (GtkTreeModelFilterVisibleFunc) match_category, cat, NULL);
        gtk_tree_view_set_model (GTK_TREE_VIEW (pack_tv), GTK_TREE_MODEL (fpackages));
        g_free (cat);
    } 
}

static void install_toggled (GtkCellRendererToggle *cell, gchar *path, gpointer user_data)
{
    gboolean val;
    GtkTreeIter iter, citer;
    GtkTreeModel *model, *cmodel;
    
    model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_data));
    gtk_tree_model_get_iter_from_string (model, &iter, path);
    
    if (GTK_IS_TREE_MODEL_FILTER (model))
    {
        cmodel = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));
        gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model), &citer, &iter);
        gtk_tree_model_get (cmodel, &citer, 2, &val, -1);
        gtk_list_store_set (GTK_LIST_STORE (cmodel), &citer, 2, 1-val, -1);
    }
    else
    {
        gtk_tree_model_get (model, &iter, 2, &val, -1);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter, 2, 1-val, -1);
    }
    category_selected (GTK_TREE_VIEW (cat_tv), NULL);
}

static void cancel (GtkButton* btn, gpointer ptr)
{
    gtk_main_quit ();
}


static void remove_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    gtk_main_quit ();
}

static void install_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    if (uninst)
    {
        message (_("Removing packages - please wait..."), 0 , -1);

        pk_task_remove_packages_async (task, puninst, TRUE, TRUE, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) remove_done, NULL);
    }
    else gtk_main_quit ();
}

static void install (GtkButton* btn, gpointer ptr)
{
    PkTask *task;
    GtkTreeIter iter;
    gboolean valid, state;
    gchar *id;

    inst = 0;
    uninst = 0;
    pinst = malloc (sizeof (gchar *));
    pinst[inst] = NULL;
    pinst = malloc (sizeof (gchar *));
    pinst[uninst] = NULL;

    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
    while (valid)
    {
        gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, 2, &state, 5, &id, -1);
        if (!strstr (id, ";installed:"))
        {
            if (state)
            {
                // needs install
                pinst = realloc (pinst, (inst + 1) * sizeof (gchar *));
                pinst[inst] = id;
                inst++;
                pinst[inst] = NULL;
            }
        }
        else
        {
            if (!state)
            {
                // needs uninstall
                puninst = realloc (puninst, (uninst + 1) * sizeof (gchar *));
                puninst[uninst] = id;
                uninst++;
                puninst[uninst] = NULL;
            }
        }
        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
    }

    if (inst)
    {
        message (_("Installing packages - please wait..."), 0 , -1);

        task = pk_task_new ();
        pk_task_install_packages_async (task, pinst, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) install_done, NULL);
    }
    else if (uninst)
    {
        message (_("Removing packages - please wait..."), 0 , -1);

        task = pk_task_new ();
        pk_task_remove_packages_async (task, puninst, TRUE, TRUE, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) remove_done, NULL);
    }
    else gtk_main_quit ();
}

/* The dialog... */

int main (int argc, char *argv[])
{
    GtkBuilder *builder;
    GtkWidget *wid;
    GtkCellRenderer *crp, *crt, *crb;
    int res;

#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    // GTK setup
    gdk_threads_init ();
    gdk_threads_enter ();
    gtk_init (&argc, &argv);
    gtk_icon_theme_prepend_search_path (gtk_icon_theme_get_default(), PACKAGE_DATA_DIR);

    // build the UI
    builder = gtk_builder_new ();
    gtk_builder_add_from_file (builder, PACKAGE_DATA_DIR "/rp_prefapps.ui", NULL);

    main_dlg = (GtkWidget *) gtk_builder_get_object (builder, "window1");
    cat_tv = (GtkWidget *) gtk_builder_get_object (builder, "treeview1");
    pack_tv = (GtkWidget *) gtk_builder_get_object (builder, "treeview2");
    cancel_btn = (GtkWidget *) gtk_builder_get_object (builder, "button1");
    install_btn = (GtkWidget *) gtk_builder_get_object (builder, "button2");

    // create list stores
    categories = gtk_list_store_new (2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
    packages = gtk_list_store_new (7, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    // set up tree views
    crp = gtk_cell_renderer_pixbuf_new ();
    crt = gtk_cell_renderer_text_new ();
    crb = gtk_cell_renderer_toggle_new ();

    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (cat_tv), 0, "Icon", crp, "pixbuf", 0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (cat_tv), 1, "Category", crt, "text", 1, NULL);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (cat_tv), FALSE);
    gtk_tree_view_set_model (GTK_TREE_VIEW (cat_tv), GTK_TREE_MODEL (categories));

    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (pack_tv), 0, "", crp, "pixbuf", 0, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (pack_tv), 1, "Description", crt, "markup", 1, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (pack_tv), 2, "Install", crb, "active", 2, NULL);

    gtk_tree_view_column_set_sizing (gtk_tree_view_get_column (GTK_TREE_VIEW (pack_tv), 0), GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width (gtk_tree_view_get_column (GTK_TREE_VIEW (pack_tv), 0), 45);
    gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (pack_tv), 1), TRUE);
    g_object_set (crt, "wrap-mode", PANGO_WRAP_WORD, "wrap-width", 400, NULL);

    g_signal_connect (cat_tv, "cursor-changed", G_CALLBACK (category_selected), NULL);
    g_signal_connect (crb, "toggled", G_CALLBACK (install_toggled), pack_tv);
    g_signal_connect (cancel_btn, "clicked", G_CALLBACK (cancel), NULL);
    g_signal_connect (install_btn, "clicked", G_CALLBACK (install), NULL);
    g_signal_connect (main_dlg, "delete_event", G_CALLBACK (cancel), NULL);

    gtk_window_set_default_size (GTK_WINDOW (main_dlg), 600, 400);
    gtk_widget_show_all (main_dlg);

    // load the data file and check with backend
    g_idle_add (read_data_file, NULL);
    gtk_main ();

    g_object_unref (builder);
    gtk_widget_destroy (main_dlg);
    gdk_threads_leave ();
    return 0;
}

