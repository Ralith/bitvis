#include <stdio.h>
#include <gtk/gtk.h>

enum {
  COL_TIME,
  COL_PRICE,
  COL_VOL,
  COL_CURR,
  COL_SYM,
  COL_COUNT
};

GtkListStore *trade_store;

GtkWidget *load_ui() {
  GError *e = NULL;
  GtkWidget *root;
  GtkBuilder* builder = gtk_builder_new();
  gtk_builder_add_from_file(builder, "main.glade", &e);
  if(e != NULL) {
    fprintf(stderr, "Failed to load UI description: %s\n", e->message);
    g_error_free(e);
    return NULL;
  }

  /* Get root window */
  root = GTK_WIDGET(gtk_builder_get_object(builder, "main"));

  trade_store = gtk_list_store_new(COL_COUNT, G_TYPE_INT64, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_STRING, G_TYPE_STRING);

  GtkTreeView *view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "treeview1"));
  g_object_unref(builder);

  /* Configure tree view */
  gtk_tree_view_set_model(view, GTK_TREE_MODEL(trade_store));

  GtkTreeViewColumn *column;

  column = gtk_tree_view_column_new_with_attributes(
    "Time",
    gtk_cell_renderer_text_new(),
    "text", COL_TIME,
    NULL);
  gtk_tree_view_append_column(view, column);
  column = gtk_tree_view_column_new_with_attributes(
    "Price",
    gtk_cell_renderer_text_new(),
    "text", COL_PRICE,
    NULL);
  gtk_tree_view_append_column(view, column);
  column = gtk_tree_view_column_new_with_attributes(
    "Volume",
    gtk_cell_renderer_text_new(),
    "text", COL_VOL,
    NULL);
  gtk_tree_view_append_column(view, column);
  column = gtk_tree_view_column_new_with_attributes(
    "Currency",
    gtk_cell_renderer_text_new(),
    "text", COL_CURR,
    NULL);
  gtk_tree_view_append_column(view, column);
  column = gtk_tree_view_column_new_with_attributes(
    "Symbol",
    gtk_cell_renderer_text_new(),
    "text", COL_SYM,
    NULL);
  gtk_tree_view_append_column(view, column);


  g_signal_connect(root, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  
  return root;
}

void record_trade(long long time, float price, float vol, const char *currency, const char *symbol) {
  GtkTreeIter iter;
  gtk_list_store_append(trade_store, &iter);

  gtk_list_store_set(trade_store, &iter,
                     COL_TIME, time,
                     COL_PRICE, price,
                     COL_VOL, vol,
                     COL_CURR, currency,
                     COL_SYM, symbol,
                     -1);
}

int main(int argc, char **argv) {
  gtk_init(&argc, &argv);

  GtkWidget *mainwin = load_ui();
  if(!mainwin) {
    return 1;
  }

  record_trade(42, 5.17, 100, "USD", "fake");

  gtk_widget_show_all(mainwin);

  gtk_main();

  return 0;
}
