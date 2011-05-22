#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <gtk/gtk.h>
#include <json-glib/json-glib.h>
#include <time.h>

#include "net.h"

#define WEBSOCKET_HOST "websocket.mtgox.com"

#define WEBSOCKET_HELLO                         \
  "GET /mtgox HTTP/1.1\r\n"                     \
  "Upgrade: WebSocket\r\n"                      \
  "Connection: Upgrade\r\n"                     \
  "Host: " WEBSOCKET_HOST "\r\n"                \
  "Origin: null\r\n"                            \
  "\r\n"

#define WEBSOCKET_REPLY                                     \
  "HTTP/1.1 101 Web Socket Protocol Handshake\r\n"          \
  "Upgrade: WebSocket\r\n"                                  \
  "Connection: Upgrade\r\n"                                 \
  "WebSocket-Origin: null\r\n"                              \
  "WebSocket-Location: ws://websocket.mtgox.com/mtgox\r\n"  \
  "\r\n"


enum {
  COL_TIME,
  COL_PRICE,
  COL_VOL,
  COL_COUNT
};

typedef enum {
  CHAN_TICKER,
  CHAN_TRADE,
  CHAN_DEPTH,
  CHAN_ACCT
} channel;

#define TRADE_CHANNEL  "dbf1dee9-4f2e-4a08-8cb7-748919a71b21"
#define TICKER_CHANNEL "d5f06780-30a8-4a48-a2f8-7ed181b4a13f"
#define DEPTH_CHANNEL  "24e67e0d-1cad-4cc0-9e7a-f8523ef460fe"

GtkListStore *trade_store;
GIOChannel *stream = NULL;
GtkStatusbar *statusbar;
struct addrinfo *servinfo;
gchar *readbuf;
size_t readbuf_end = 0;
size_t readbuf_size = 1024;

struct {
  GtkLabel *high, *low, *buy, *sell, *volume;
} ticker;

void on_view_change(GtkWidget *widget, GtkAllocation *allocation, gpointer user_data) {
  GtkTreeView *view = GTK_TREE_VIEW(widget);
  GtkAdjustment *vadj = gtk_tree_view_get_vadjustment(view);
  gtk_adjustment_set_value(vadj, gtk_adjustment_get_upper(vadj) - gtk_adjustment_get_page_size(vadj));
}

char *asprintfx(const char *fmt,  ...) {
  char *dest;
  va_list ap;
  int len;

  dest = NULL;

  va_start(ap, fmt);
  len = vsnprintf(dest, 0, fmt, ap);
  va_end(ap);

  dest = (char*)malloc(len + 1);
  va_start(ap, fmt);
  vsprintf(dest, fmt, ap);
  va_end(ap);

  return dest;
}

GtkWindow *load_ui() {
  GError *e;
  GtkBuilder* builder = gtk_builder_new();
  const gchar * const *datadirs = g_get_system_data_dirs();
  const gchar * const *dir;
  for(dir = datadirs; *dir != NULL; ++dir) {
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
      "Failed to locate UI description (main.glade). Verify that the program is properly installed.");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_error_free(e);
    return NULL;
  }

  /* Get root window */
  GtkWindow *root = GTK_WINDOW(gtk_builder_get_object(builder, "main"));

  GtkTreeView *view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "treeview1"));
  statusbar = GTK_STATUSBAR(gtk_builder_get_object(builder, "statusbar"));

  /* Bind ticker */
  ticker.buy = GTK_LABEL(gtk_builder_get_object(builder, "tickbuy"));
  ticker.sell = GTK_LABEL(gtk_builder_get_object(builder, "ticksell"));
  ticker.low = GTK_LABEL(gtk_builder_get_object(builder, "ticklow"));
  ticker.high = GTK_LABEL(gtk_builder_get_object(builder, "tickhigh"));
  ticker.volume = GTK_LABEL(gtk_builder_get_object(builder, "tickvolume"));

  /* Done obtaining handles */
  g_object_unref(builder);

  /* Configure tree view */
  trade_store = gtk_list_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_FLOAT, G_TYPE_FLOAT);
  g_signal_connect(view, "size-allocate", G_CALLBACK(on_view_change), NULL);
  gtk_tree_view_set_model(view, GTK_TREE_MODEL(trade_store));

  GtkTreeViewColumn *column;

  GtkCellRenderer *r = gtk_cell_renderer_text_new();
  /* Align the following right */
  gtk_cell_renderer_set_alignment(r, 1.0, 0.5);
  column = gtk_tree_view_column_new_with_attributes(
    "Time",
    r,
    "text", COL_TIME,
    NULL);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_append_column(view, column);
  column = gtk_tree_view_column_new_with_attributes(
    "Price",
    r,
    "text", COL_PRICE,
    NULL);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_append_column(view, column);
  column = gtk_tree_view_column_new_with_attributes(
    "Volume",
    r,
    "text", COL_VOL,
    NULL);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_append_column(view, column);
  gtk_tree_view_append_column(view, gtk_tree_view_column_new());

  g_signal_connect(root, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  
  return root;
}

void record_trade(gint64 time, float price, float vol) {
  GtkTreeIter iter;
  gtk_list_store_append(trade_store, &iter);

  char buf[128];
  {
    time_t systime = time;
    struct tm *local = localtime(&systime);
    strftime(buf, 128, "%b %d %k:%M:%S", local);
  }

  gtk_list_store_set(trade_store, &iter,
                     COL_TIME, buf,
                     COL_PRICE, price,
                     COL_VOL, vol,
                     -1);
}

void update_ticker(gfloat buy, gfloat sell, gfloat low, gfloat high, gint64 volume) {
  char *text;

  text = asprintfx("<b>Bid:</b> %f", buy);
  gtk_label_set_markup(ticker.buy, text);
  free(text);

  text = asprintfx("<b>Ask:</b> %f", sell);
  gtk_label_set_markup(ticker.sell, text);
  free(text);

  text = asprintfx("<b>Low:</b> %f", low);
  gtk_label_set_markup(ticker.low, text);
  free(text);

  text = asprintfx("<b>High:</b> %f", high);
  gtk_label_set_markup(ticker.high, text);
  free(text);

  text = asprintfx("<b>Volume:</b> %d", volume);
  gtk_label_set_markup(ticker.volume, text);
  free(text);
}

gboolean tick(GIOChannel *, GIOCondition, gpointer);

gboolean json_reader_read_member_checked(JsonReader *reader, const gchar *member) {
  if(!json_reader_read_member(reader, member)) {
    const GError *e = json_reader_get_error(reader);
    GtkWidget *dialog = gtk_message_dialog_new(
      NULL,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to parse data from server:\n%s",
      e->message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return FALSE;
  }
  return TRUE;
}

gdouble json_reader_get_number_value(JsonReader *reader) {
  JsonNode *v = json_reader_get_value(reader);

  if(!JSON_NODE_HOLDS_VALUE(v)) {
    return 0;
  }

  GType t = json_node_get_value_type(v);
  switch(t) {
  case G_TYPE_INT64:
    return json_node_get_int(v);

  case G_TYPE_DOUBLE:
    return json_node_get_double(v);

  default:
    return 0;
  } 
}

void read_json(const gchar *buf, gsize len) {
  JsonParser *parser = json_parser_new();
  GError *error = NULL;
  gboolean res;

  res = json_parser_load_from_data(parser, buf, len, &error);
  if (!res)
    g_error ("Unable to parse JSON: %s", error->message);

  JsonReader *reader = json_reader_new(json_parser_get_root(parser));

  /* read the JSON tree */
  json_reader_read_member_checked(reader, "channel");
  const char *key = json_reader_get_string_value(reader);
  channel chan;
  if(!strcmp(key, TRADE_CHANNEL)) {
    chan = CHAN_TRADE;
  } else if(!strcmp(key, TICKER_CHANNEL)) {
    chan = CHAN_TICKER;
  } else if(!strcmp(key, DEPTH_CHANNEL)) {
    chan = CHAN_DEPTH;
  } else {
    chan = CHAN_ACCT;
  }
  json_reader_end_member(reader);

  res = json_reader_read_member(reader, "op");
  if(!res || !strcmp(json_reader_get_string_value(reader), "private")) {
    json_reader_end_member(reader);
    /* We should have data */
    switch(chan) {
    case CHAN_TRADE: {
      if(!json_reader_read_member_checked(reader, "trade")) {
        break;
      }

      gfloat amt = -1;
      if(json_reader_read_member_checked(reader, "amount")) {
        amt = json_reader_get_number_value(reader);
      }
      json_reader_end_member(reader);

      gint64 date = -1;
      if(json_reader_read_member_checked(reader, "date")) {
        date = json_reader_get_int_value(reader);
      }
      json_reader_end_member(reader);

      gfloat price = -1;
      if(json_reader_read_member_checked(reader, "price")) {
        price = json_reader_get_number_value(reader);
      }
      json_reader_end_member(reader);

      json_reader_end_member(reader);

      record_trade(date, price, amt);
      break;
    }
    case CHAN_TICKER: {
      json_reader_read_member_checked(reader, "ticker");

      gfloat buy = -1;
      if(json_reader_read_member_checked(reader, "buy")) {
        buy = json_reader_get_number_value(reader);
      }
      json_reader_end_member(reader);

      gfloat high = -1;
      if(json_reader_read_member_checked(reader, "high")) {
        high = json_reader_get_number_value(reader);
      }
      json_reader_end_member(reader);

      gfloat low = -1;
      if(json_reader_read_member_checked(reader, "low")) {
        low = json_reader_get_number_value(reader);
      }
      json_reader_end_member(reader);

      gfloat sell = -1;
      if(json_reader_read_member_checked(reader, "sell")) {
        sell = json_reader_get_number_value(reader);
      }
      json_reader_end_member(reader);

      gint64 vol = -1;
      if(json_reader_read_member_checked(reader, "vol")) {
        vol = json_reader_get_int_value(reader);
      }
      json_reader_end_member(reader);

      json_reader_end_member(reader);

      update_ticker(buy, sell, low, high, vol);
    }
    case CHAN_DEPTH:
    case CHAN_ACCT:
      break;
    }
  }
  json_reader_end_member(reader);

  g_object_unref(reader);
  g_object_unref(parser);

}

gboolean do_connect(struct addrinfo *i) {
  const char *connctx = "Connection status";
  static struct addrinfo *current;
  struct addrinfo *p;
  if(i != NULL) {
    current = i;
  }
  gtk_statusbar_push(statusbar, gtk_statusbar_get_context_id(statusbar, connctx), "Connecting...");
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
        stream = g_io_channel_unix_new(fd);
        g_io_channel_set_encoding(stream, NULL, NULL);
        g_io_channel_set_flags(stream, G_IO_FLAG_NONBLOCK, NULL);
        g_io_add_watch(stream, G_IO_OUT, tick, (gpointer)connctx);
        break;
      } else {
        close(fd);
        perror("connect()");
        continue;
      }
    } else {
      gtk_statusbar_push(statusbar, gtk_statusbar_get_context_id(statusbar, connctx), "Handshaking...");
      stream = g_io_channel_unix_new(fd);
      g_io_channel_set_encoding(stream, NULL, NULL);
      g_io_channel_set_flags(stream, G_IO_FLAG_NONBLOCK, NULL);
      g_io_add_watch(stream, G_IO_OUT, tick, (gpointer)connctx);
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

gboolean handshake(GIOChannel *ch) {
  static gsize total_written = 0;
  const gsize len = strlen(WEBSOCKET_HELLO);
  gsize new_written;
  GError *e = NULL;

  if(total_written == len) {
    /* We already returned true, so this must be a reconnect */
    total_written = 0;
  }
  
  GIOStatus s = g_io_channel_write_chars(ch, WEBSOCKET_HELLO+total_written, len-total_written, &new_written, &e);
  
  if(s != G_IO_STATUS_NORMAL && s != G_IO_STATUS_AGAIN) {
    GtkWidget *dialog = gtk_message_dialog_new(
      NULL,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to handshake with market data server:\n%s",
      e->message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    gtk_main_quit();
  }

  e = NULL;
  s = g_io_channel_flush(ch, &e);

  if(s != G_IO_STATUS_NORMAL && s != G_IO_STATUS_AGAIN) {
    GtkWidget *dialog = gtk_message_dialog_new(
      NULL,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to handshake with market data server:\n%s",
      e->message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    gtk_main_quit();
  }
  
  total_written += new_written;
  
  if(total_written == len) {
    return TRUE;
  }
  return FALSE;
}

int try_connect() {
  int res = resolve(WEBSOCKET_HOST, "80", &servinfo);
  if(res != 0) {
    GtkWidget *dialog = gtk_message_dialog_new(
      NULL,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Failed to resolve market address:\n%s",
      gai_strerror(res));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return 1;
  }
  
  if(!do_connect(servinfo)) {
    return 2;
  }

  return 0;
}

gboolean tick(GIOChannel *source, GIOCondition condition, gpointer data) {
  if(condition == G_IO_OUT) {
    gtk_statusbar_push(statusbar, gtk_statusbar_get_context_id(statusbar, (const char*)data), "Handshaking...");
    if(servinfo) {
      freeaddrinfo(servinfo); // all done with this structure
      servinfo = NULL;
    }
    if(handshake(source)) {
      g_io_add_watch(stream, G_IO_IN, tick, data);
      gtk_statusbar_pop(statusbar, gtk_statusbar_get_context_id(statusbar, (const char*)data));
      gtk_statusbar_push(statusbar, gtk_statusbar_get_context_id(statusbar, (const char*)data), "Connected!");
      /* Remove watch */
      return FALSE;
    }
    return TRUE;
  }

  GError *e = NULL;
  guint bytes;
  GIOStatus s = g_io_channel_read_chars(stream,
                                        readbuf+readbuf_end,
                                        readbuf_size-readbuf_end,
                                        &bytes, &e);
  if(s != G_IO_STATUS_NORMAL) {
    gtk_statusbar_pop(statusbar, gtk_statusbar_get_context_id(statusbar, (const char*)data));

    GtkWidget *dialog = gtk_message_dialog_new(
      NULL,
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      "Lost connection to market data source:\n%s\nAttempting to reconnect.",
      e->message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    e = NULL;

    g_io_channel_shutdown(stream, FALSE, &e);
    stream = NULL;
    
    if(try_connect()) {
      gtk_main_quit();
    }
  }
  
  gsize i;
  static gssize begin = -1;
  const gsize total = readbuf_end + bytes;
  for(i = readbuf_end; i < total; ++i) {
    if(begin < 0) {
      if(readbuf[i] == (gchar)0x00) {
        begin = i + 1;
      }
    } else {
      if(readbuf[i] == (gchar)0xFF) {
        read_json(readbuf+begin, i-begin);
        begin = -1;
      }
    }
  }

  if(begin < 0) {
    readbuf_end = 0;
  } else {
    if(begin == 0 && total == readbuf_size) {
      readbuf_size *= 2;
      readbuf = realloc(readbuf, readbuf_size*sizeof(gchar));
    }
    readbuf_end = total-begin;
    memmove(readbuf, readbuf+begin, readbuf_end*sizeof(gchar));
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

  readbuf = calloc(readbuf_size, sizeof(gchar));

  if(try_connect()) {
    return 1;
  }

  if(argc == 2 && !strcmp("test", argv[1])) {
    unsigned i;
    for(i = 0; i < 100; ++i) {
      record_trade(i, i*0.1, i*1.4);
    }
  }

  gtk_main();

  return 0;
}
