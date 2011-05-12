#include <stdio.h>
#include <gtk/gtk.h>

int main(int argc, char *argv[])
{
  GError *e = NULL;
  gtk_init(&argc, &argv);

  GtkWidget *mainwin = NULL;
  
  GtkBuilder* builder = gtk_builder_new();
  gtk_builder_add_from_file(builder, "main.glade", &e);
  if(e != NULL) {
    fprintf(stderr, "Failed to load UI description: %s\n", e->message);
    g_error_free(e);
    return -1;
  }

  mainwin = (GtkWidget*)gtk_builder_get_object(builder, "main");

  g_object_unref(builder);

  
  g_signal_connect(mainwin, "destroy",
                   G_CALLBACK(gtk_main_quit), NULL);

  gtk_widget_show_all(mainwin);

  gtk_main();

  return 0;
}
