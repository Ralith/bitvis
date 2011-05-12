#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>

#include "net.h"

enum {
  COL_TIME,
  COL_PRICE,
  COL_VOL,
  COL_CURR,
  COL_SYM,
  COL_COUNT
};

GtkListStore *trade_store;
GIOChannel *ticker = NULL;
GtkLabel *statuslabel;
struct addrinfo *servinfo;
gchar *readbuf;
size_t readbuf_end = 0;
size_t readbuf_size = 1024;

void on_trade_insert(GtkTreeModel *tree_model,
                     GtkTreePath  *path,
                     GtkTreeIter  *iter,
                     gpointer      user_data) {
  GtkTreeView *view = GTK_TREE_VIEW(user_data);
  gtk_tree_view_scroll_to_cell(view, path, NULL, FALSE, 0, 0);
}

char *asprintfx(const char *fmt,  ...) {
	char *dest;
	va_list ap;
	int len;

	dest = NULL;

	va_start(ap, fmt);
	len = vsnprintf(dest, 0, fmt, ap);

	dest = (char*)malloc(len + 1);
	vsprintf(dest, fmt, ap);

	va_end(ap);

	return dest;
}

GtkWindow *load_ui() {
  GError *e;
  GtkBuilder* builder = gtk_builder_new();
  const gchar * const *datadirs = g_get_system_data_dirs();
  const gchar * const *dir;
  for(dir = datadirs; datadirs != NULL; ++dir) {
    char *path = asprintfx("%s/bitvis/%s", *dir, "main.glade");
    e = NULL;
    gtk_builder_add_from_file(builder, path, &e);
    free(path);
    if(e != NULL) {
      if(!g_error_matches(e, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        GtkWidget *dialog = gtk_message_dialog_new(
          NULL,
          GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_MESSAGE_ERROR,
          GTK_BUTTONS_CLOSE,
          "Unable to check %s for installed data files:\n%s",
          *dir, e->message);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_error_free(e);
      }
      continue;
    }
    break;
  }
  
  if(e != NULL) {
    GtkWidget *dialog = gtk_message_dialog_new(
      NULL,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to locate UI description (main.glade). verify that the program is properly installed");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_error_free(e);
    return NULL;
  }

  /* Get root window */
  GtkWindow *root = GTK_WINDOW(gtk_builder_get_object(builder, "main"));

  GtkTreeView *view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "treeview1"));
  statuslabel = GTK_LABEL(gtk_builder_get_object(builder, "statuslabel"));

  g_object_unref(builder);

  /* Configure tree view */
  trade_store = gtk_list_store_new(COL_COUNT, G_TYPE_INT64, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_STRING, G_TYPE_STRING);
  g_signal_connect(trade_store, "row-inserted", G_CALLBACK(on_trade_insert), view);
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

void record_trade(gint64 time, float price, float vol, const gchar *currency, const gchar *symbol) {
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

gboolean tick(GIOChannel *, GIOCondition, gpointer);

void read_json(const gchar *buf, gsize len) {
  JsonParser *parser = json_parser_new();
  GError *error = NULL;
  gboolean res;

  res = json_parser_load_from_data(parser, buf, len, &error);
  if (!res)
    g_error ("Unable to parse JSON: %s", error->message);

  JsonReader *reader = json_reader_new(json_parser_get_root(parser));

  /* read the JSON tree */
  json_reader_read_member(reader, "timestamp");
  gint64 time = json_reader_get_int_value(reader);
  json_reader_end_element(reader);
  json_reader_read_member(reader, "price");
  float price = strtof(json_reader_get_string_value(reader), NULL);
  json_reader_end_element(reader);
  json_reader_read_member(reader, "volume");
  float vol = strtof(json_reader_get_string_value(reader), NULL);
  json_reader_end_element(reader);
  json_reader_read_member(reader, "currency");
  gchar curr[64];
  memcpy(curr, json_reader_get_string_value(reader),
         64*sizeof(gchar));
  json_reader_end_element(reader);
  json_reader_read_member(reader, "symbol");
  gchar sym[64];
  memcpy(sym, json_reader_get_string_value(reader),
         64*sizeof(gchar));
  json_reader_end_element(reader);

  g_object_unref(reader);
  g_object_unref(parser);

  record_trade(time, price, vol, curr, sym);
}

gboolean tick(GIOChannel *source, GIOCondition condition, gpointer data) {
  if(condition == G_IO_OUT) {
    gtk_label_set_markup(statuslabel, "<span background=\"#009900\">Connected!</span>");
    g_io_add_watch(ticker, G_IO_IN, tick, NULL);
    freeaddrinfo(servinfo); // all done with this structure
    /* Remove watch */
    return FALSE;
  }

  GError *e = NULL;
  guint bytes;
  GIOStatus s = g_io_channel_read_chars(ticker,
                                        readbuf+readbuf_end,
                                        readbuf_size-readbuf_end,
                                        &bytes, &e);
  if(s != G_IO_STATUS_NORMAL) {
    g_io_channel_shutdown(ticker, FALSE, &e);
    gtk_label_set_markup(statuslabel, "<span background=\"#FF2200\">Connection lost!</span>");
  }
  ssize_t i;
  ssize_t last = 0;
  const ssize_t total = readbuf_end + bytes;
  for(i = readbuf_end; i < total; ++i) {
    if(readbuf[i] == '\0' && i-last > 0) {
      read_json(readbuf+last, i-last);
      last = i+1;
    }
  }

  if(total-last > 0) {
    if(last == 0 && total == readbuf_size) {
      readbuf_size *= 2;
      readbuf = realloc(readbuf, readbuf_size*sizeof(gchar));
    }
    readbuf_end = total-last;
    memmove(readbuf, readbuf+last, readbuf_end*sizeof(gchar));
  } else {
    readbuf_end = 0;
  }

  return TRUE;
}

gboolean try_connect(struct addrinfo *i) {
  static struct addrinfo *current;
  struct addrinfo *p;
  if(i != NULL) {
    current = i;
  }
  // loop through all the results and connect to the first we can
  int fd;
  for(p = current; p != NULL; p = p->ai_next) {
    if((fd = socket(p->ai_family, p->ai_socktype,
                    p->ai_protocol)) == -1) {
      perror("socket()");
      continue;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);

    int r = connect(fd, p->ai_addr, p->ai_addrlen);
    if(r == -1) {
      if(errno == EINPROGRESS) {
        /* Possible connection */
        current = p;
        if(ticker) {
          g_object_unref(ticker);
        }
        ticker = g_io_channel_unix_new(fd);
        g_io_channel_set_flags(ticker, G_IO_FLAG_NONBLOCK, NULL);
        g_io_add_watch(ticker, G_IO_OUT, tick, NULL);
        break;
      } else {
        close(fd);
        perror("connect()");
        continue;
      }
    } else {
      gtk_label_set_markup(statuslabel, "<span background=\"#009900\">Connected!</span>");
      if(ticker) {
        g_object_unref(ticker);
      }
      ticker = g_io_channel_unix_new(fd);
      g_io_channel_set_flags(ticker, G_IO_FLAG_NONBLOCK, NULL);
      g_io_add_watch(ticker, G_IO_IN, tick, NULL);
      freeaddrinfo(servinfo); // all done with this structure
    }

    break;
  }

  if(p == NULL) {
    GtkWidget *dialog = gtk_message_dialog_new(
      NULL,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to connect to market stream!");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return FALSE;
  }
  return TRUE;
}

int main(int argc, char **argv) {
  gtk_init(&argc, &argv);

  GtkWindow *mainwin = load_ui();
  if(!mainwin) {
    return 1;
  }

  gtk_widget_show_all(GTK_WIDGET(mainwin));

  int res = resolve("bitcoincharts.com", "27007", &servinfo);
  if(res != 0) {
    GtkWidget *dialog = gtk_message_dialog_new(
      mainwin,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to resolve market address:\n%s",
      gai_strerror(res));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return 1;
  }
  
  if(!try_connect(servinfo)) {
    return 2;
  }

  readbuf = calloc(readbuf_size, sizeof(gchar));

  gtk_main();

  return 0;
}
