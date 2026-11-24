/* Entry point of the voice-receive-only image; the processor lives in
 * proc_dpmr446_voice.cpp, shared with the combined image. */
int dpmr446_voice_entry();

int main() {
    return dpmr446_voice_entry();
}
