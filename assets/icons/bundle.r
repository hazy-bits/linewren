data 'Lnwr' (0) {
    $"084C696E657772656E" /* Pascal string: Linewren */
};

data 'BNDL' (128) {
    $"4C6E777200000001" /* Creator, owner resource ID, two types. */
    $"49434E2300010000008000010081" /* ICN# local 0/1 -> 128/129. */
    $"4652454600010000008000010081" /* FREF local 0/1 -> 128/129. */
};

data 'FREF' (128) {
    $"4150504C000000" /* APPL, icon 0, empty filename. */
};

data 'FREF' (129) {
    $"54455854000100" /* TEXT, icon 1, empty filename. */
};
