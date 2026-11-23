extern "C" {
    int hyprimate_import_value() { return 7; }
    int hyprimate_import_bias() { return 3; }
    int hyprimate_import_consumer() {
        return hyprimate_import_value() + hyprimate_import_bias();
    }
}
