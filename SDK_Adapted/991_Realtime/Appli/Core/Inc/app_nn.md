# Persistent Neural ART runtime

app_nn_init initializes runtime/network once in the NN thread. app_nn_run calls
the vendor Reset_Network API, executes until DONE, and waits only on WFE results.
User input/output addresses are assigned before each run. The generated network's
per-inference relocation callback remains active, so rotating buffers remain valid.
The old single-inference template recreated ThreadX resources and recopied the
network epoch program for every frame. Model operators and weights are unchanged.

This matches the local vendor API and its generated ec_network_init comment that
network initialization runs once. Host lifecycle tests and target compilation
verify the calling sequence; actual multi-frame inference requires board testing.
