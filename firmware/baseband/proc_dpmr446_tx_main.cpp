/* Entry point of the transmit-only image; the processor lives in
 * proc_dpmr446_tx.cpp, shared with the combined image. */
int dpmr446_tx_entry();

int main() {
    return dpmr446_tx_entry();
}
