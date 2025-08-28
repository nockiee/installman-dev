#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <archive.h>
#include <archive_entry.h>
#include <thread>
#include <string>
#include <vector>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <fstream>

struct AppData {
    GtkWidget *window;
    GtkWidget *progress_bar;
    GtkWidget *progress_label;
    GtkWidget *log_text;
    GtkTextBuffer *log_buffer;
    GtkWidget *install_button;
    GtkWidget *cancel_button;
    gchar *archive_path;
    gchar *temp_dir;
    gchar *install_dir;
    gboolean is_installing;
    gboolean cancel_requested;
};

void log_message(AppData *data, const gchar *message, gboolean is_error = FALSE) {
    g_idle_add([](gpointer user_data) -> gboolean {
        auto *log_data = static_cast<std::pair<AppData*, std::pair<std::string, bool>>*>(user_data);
        AppData *app_data = log_data->first;
        std::string message = log_data->second.first;
        bool is_error = log_data->second.second;
        
        GtkTextIter end_iter;
        gtk_text_buffer_get_end_iter(app_data->log_buffer, &end_iter);
        
        if (is_error) {
            GtkTextTag *error_tag = gtk_text_buffer_create_tag(app_data->log_buffer, "error", "foreground", "red", NULL);
            gtk_text_buffer_insert_with_tags(app_data->log_buffer, &end_iter, message.c_str(), -1, error_tag, NULL);
        } else {
            gtk_text_buffer_insert(app_data->log_buffer, &end_iter, message.c_str(), -1);
        }
        
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(app_data->log_text), gtk_text_buffer_get_insert(app_data->log_buffer), 0.0, TRUE, 0.0, 1.0);
        
        delete log_data;
        return G_SOURCE_REMOVE;
    }, new std::pair<AppData*, std::pair<std::string, bool>>(data, std::make_pair(std::string(message), is_error)));
}

void update_progress(AppData *data, gdouble fraction, const gchar *message) {
    g_idle_add([](gpointer user_data) -> gboolean {
        auto *progress_data = static_cast<std::pair<AppData*, std::pair<gdouble, std::string>>*>(user_data);
        AppData *app_data = progress_data->first;
        gdouble fraction = progress_data->second.first;
        std::string message = progress_data->second.second;
        
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app_data->progress_bar), fraction);
        gtk_label_set_text(GTK_LABEL(app_data->progress_label), message.c_str());
        
        delete progress_data;
        return G_SOURCE_REMOVE;
    }, new std::pair<AppData*, std::pair<gdouble, std::string>>(data, std::make_pair(fraction, std::string(message))));
}

void show_error(AppData *data, const gchar *message) {
    g_idle_add([](gpointer user_data) -> gboolean {
        auto *error_data = static_cast<std::pair<AppData*, std::string>*>(user_data);
        AppData *app_data = error_data->first;
        std::string message = error_data->second;
        
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(app_data->window), GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", message.c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        
        delete error_data;
        return G_SOURCE_REMOVE;
    }, new std::pair<AppData*, std::string>(data, std::string(message)));
}

gboolean extract_archive(AppData *data) {
    struct archive *a;
    struct archive_entry *entry;
    int flags;

    log_message(data, "Извлечение архива...");
    update_progress(data, 0.25, "Извлечение архива");

    data->temp_dir = g_strdup("/tmp/installman_XXXXXX");
    g_mkdtemp(data->temp_dir);
    log_message(data, g_strdup_printf("Временная директория: %s", data->temp_dir));

    flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM;

    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, data->archive_path, 10240) != ARCHIVE_OK) {
        log_message(data, "Ошибка открытия архива", TRUE);
        return FALSE;
    }

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (data->cancel_requested) {
            log_message(data, "Извлечение отменено пользователем", TRUE);
            archive_read_free(a);
            return FALSE;
        }

        const gchar *current_file = archive_entry_pathname(entry);
        gchar *output_path = g_build_filename(data->temp_dir, current_file, NULL);
        
        archive_entry_set_pathname(entry, output_path);
        int r = archive_read_extract(a, entry, flags);
        
        if (r != ARCHIVE_OK) {
            log_message(data, g_strdup_printf("Ошибка извлечения: %s", current_file), TRUE);
            g_free(output_path);
            archive_read_free(a);
            return FALSE;
        }
        
        g_free(output_path);
    }

    archive_read_free(a);
    log_message(data, "Архив успешно извлечен");
    return TRUE;
}

gchar* find_configure_script(AppData *data) {
    GDir *dir = g_dir_open(data->temp_dir, 0, NULL);
    if (!dir) return NULL;

    const gchar *name;
    gchar *configure_path = NULL;

    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *full_path = g_build_filename(data->temp_dir, name, NULL);
        
        if (g_file_test(full_path, G_FILE_TEST_IS_DIR)) {
            gchar *configure_test = g_build_filename(full_path, "configure", NULL);
            if (g_file_test(configure_test, G_FILE_TEST_IS_EXECUTABLE)) {
                configure_path = configure_test;
                g_free(full_path);
                break;
            }
            g_free(configure_test);
        }
        g_free(full_path);
    }

    g_dir_close(dir);
    return configure_path;
}

gboolean execute_shell_command(AppData *data, const gchar *command) {
    log_message(data, g_strdup_printf("Выполнение: %s", command));
    
    gchar *output = NULL;
    gchar *errors = NULL;
    gint exit_status = 0;
    GError *error = NULL;

    gboolean result = g_spawn_command_line_sync(command, &output, &errors, &exit_status, &error);
    
    if (output && strlen(output) > 0) log_message(data, output);
    if (errors && strlen(errors) > 0) log_message(data, errors, TRUE);
    
    if (error) {
        log_message(data, error->message, TRUE);
        g_error_free(error);
        result = FALSE;
    }
    
    if (exit_status != 0) {
        log_message(data, g_strdup_printf("Код ошибки: %d", exit_status), TRUE);
        result = FALSE;
    }
    
    g_free(output);
    g_free(errors);
    return result;
}

gboolean build_program(AppData *data) {
    gchar *configure_script = find_configure_script(data);
    gchar *source_dir = NULL;

    GDir *dir = g_dir_open(data->temp_dir, 0, NULL);
    if (dir) {
        const gchar *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            gchar *full_path = g_build_filename(data->temp_dir, name, NULL);
            if (g_file_test(full_path, G_FILE_TEST_IS_DIR) && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                source_dir = full_path;
                break;
            }
            g_free(full_path);
        }
        g_dir_close(dir);
    }

    if (!source_dir) {
        log_message(data, "Директория с исходным кодом не найдена", TRUE);
        return FALSE;
    }

    if (configure_script) {
        log_message(data, "Найден скрипт configure");
        update_progress(data, 0.5, "Конфигурация сборки");
        
        gchar *configure_cmd = g_strdup_printf("cd \"%s\" && ./configure --prefix=%s", source_dir, data->install_dir);
        if (!execute_shell_command(data, configure_cmd)) {
            g_free(configure_cmd);
            g_free(configure_script);
            g_free(source_dir);
            return FALSE;
        }
        g_free(configure_cmd);
        g_free(configure_script);
    }

    update_progress(data, 0.75, "Компиляция");
    log_message(data, "Запуск компиляции...");
    
    gchar *make_cmd = g_strdup_printf("cd \"%s\" && make -j2", source_dir);
    gboolean result = execute_shell_command(data, make_cmd);
    g_free(make_cmd);
    g_free(source_dir);

    if (result) log_message(data, "Компиляция завершена успешно");
    return result;
}

gboolean install_program(AppData *data) {
    update_progress(data, 0.9, "Установка");
    log_message(data, "Установка программы...");
    
    gchar *source_dir = NULL;
    GDir *dir = g_dir_open(data->temp_dir, 0, NULL);
    if (dir) {
        const gchar *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            gchar *full_path = g_build_filename(data->temp_dir, name, NULL);
            if (g_file_test(full_path, G_FILE_TEST_IS_DIR) && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                gchar *makefile_path = g_build_filename(full_path, "Makefile", NULL);
                if (g_file_test(makefile_path, G_FILE_TEST_EXISTS)) {
                    source_dir = full_path;
                    g_free(makefile_path);
                    break;
                }
                g_free(makefile_path);
            }
            g_free(full_path);
        }
        g_dir_close(dir);
    }

    if (!source_dir) {
        log_message(data, "Makefile не найден", TRUE);
        return FALSE;
    }

    gchar *install_cmd = g_strdup_printf("cd \"%s\" && sudo make install", source_dir);
    gboolean result = execute_shell_command(data, install_cmd);
    g_free(install_cmd);
    g_free(source_dir);

    if (result) {
        log_message(data, "Установка завершена успешно!");
        update_progress(data, 1.0, "Установка завершена");
    }
    return result;
}

void cleanup(AppData *data) {
    if (data->temp_dir && g_file_test(data->temp_dir, G_FILE_TEST_IS_DIR)) {
        gchar *command = g_strdup_printf("rm -rf \"%s\"", data->temp_dir);
        execute_shell_command(data, command);
        g_free(command);
        log_message(data, "Временные файлы удалены");
    }
}

void installation_thread(AppData *data) {
    data->is_installing = TRUE;
    data->cancel_requested = FALSE;
    
    if (!extract_archive(data)) goto cleanup;
    if (data->cancel_requested) goto cleanup;
    if (!build_program(data)) goto cleanup;
    if (data->cancel_requested) goto cleanup;
    if (!install_program(data)) goto cleanup;
    
cleanup:
    cleanup(data);
    data->is_installing = FALSE;
    
    g_idle_add([](gpointer user_data) -> gboolean {
        AppData *data = static_cast<AppData*>(user_data);
        gtk_widget_set_sensitive(data->install_button, TRUE);
        gtk_widget_set_sensitive(data->cancel_button, TRUE);
        gtk_button_set_label(GTK_BUTTON(data->cancel_button), "Закрыть");
        return G_SOURCE_REMOVE;
    }, data);
}

void on_install_clicked(GtkWidget *widget, gpointer user_data) {
    AppData *data = static_cast<AppData*>(user_data);
    
    if (!data->archive_path) {
        show_error(data, "Не выбран архив для установки");
        return;
    }
    
    if (data->is_installing) return;
    
    std::thread thread(installation_thread, data);
    thread.detach();
}

void on_cancel_clicked(GtkWidget *widget, gpointer user_data) {
    AppData *data = static_cast<AppData*>(user_data);
    
    if (data->is_installing) {
        data->cancel_requested = TRUE;
        log_message(data, "Запрос на отмену установки...");
    } else {
        gtk_main_quit();
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    gchar *archive_path = NULL;
    if (argc > 1) {
        archive_path = argv[1];
        if (!g_file_test(archive_path, G_FILE_TEST_IS_REGULAR)) {
            g_printerr("Ошибка: файл %s не существует\n", archive_path);
            return 1;
        }
    }
    
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Installman - Установщик программ");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 500);
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), main_box);
    
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), "<span size='x-large' weight='bold'>Installman</span>");
    gtk_label_set_justify(GTK_LABEL(title_label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(main_box), title_label, FALSE, FALSE, 0);
    
    GtkWidget *archive_info = gtk_label_new("");
    gtk_label_set_justify(GTK_LABEL(archive_info), GTK_JUSTIFY_LEFT);
    gtk_label_set_line_wrap(GTK_LABEL(archive_info), TRUE);
    gtk_box_pack_start(GTK_BOX(main_box), archive_info, FALSE, FALSE, 0);
    
    if (archive_path) {
        gchar *archive_name = g_path_get_basename(archive_path);
        GFile *file = g_file_new_for_path(archive_path);
        GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
        gsize size = g_file_info_get_size(info);
        gdouble size_mb = size / (1024.0 * 1024.0);
        
        gchar *info_text = g_strdup_printf("Архив: %s\nРазмер: %.2f MB\nПуть: %s", archive_name, size_mb, archive_path);
        gtk_label_set_text(GTK_LABEL(archive_info), info_text);
        
        g_free(archive_name);
        g_free(info_text);
        g_object_unref(info);
        g_object_unref(file);
    }
    
    GtkWidget *progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(main_box), progress_bar, FALSE, FALSE, 0);
    
    GtkWidget *progress_label = gtk_label_new("Готов к установке...");
    gtk_box_pack_start(GTK_BOX(main_box), progress_label, FALSE, FALSE, 0);
    
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled_window, -1, 200);
    gtk_box_pack_start(GTK_BOX(main_box), scrolled_window, TRUE, TRUE, 0);
    
    GtkWidget *log_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_text), GTK_WRAP_WORD);
    GtkTextBuffer *log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_text));
    
    PangoFontDescription *font_desc = pango_font_description_from_string("Monospace 10");
    gtk_widget_override_font(log_text, font_desc);
    pango_font_description_free(font_desc);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window), log_text);
    
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_set_homogeneous(GTK_BOX(button_box), TRUE);
    gtk_box_pack_start(GTK_BOX(main_box), button_box, FALSE, FALSE, 0);
    
    GtkWidget *install_button = gtk_button_new_with_label("Установить");
    GtkWidget *cancel_button = gtk_button_new_with_label("Отмена");
    
    gtk_box_pack_start(GTK_BOX(button_box), install_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), cancel_button, FALSE, FALSE, 0);
    
    AppData *app_data = g_new0(AppData, 1);
    app_data->window = window;
    app_data->progress_bar = progress_bar;
    app_data->progress_label = progress_label;
    app_data->log_text = log_text;
    app_data->log_buffer = log_buffer;
    app_data->install_button = install_button;
    app_data->cancel_button = cancel_button;
    app_data->archive_path = archive_path;
    app_data->install_dir = g_strdup("/usr/local");
    app_data->is_installing = FALSE;
    app_data->cancel_requested = FALSE;
    
    g_signal_connect(install_button, "clicked", G_CALLBACK(on_install_clicked), app_data);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), app_data);
    
    gtk_widget_show_all(window);
    gtk_main();
    
    if (app_data->temp_dir) g_free(app_data->temp_dir);
    if (app_data->install_dir) g_free(app_data->install_dir);
    g_free(app_data);
    
    return 0;
}