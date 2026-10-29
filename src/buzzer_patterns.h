#ifndef ICUBE_BUZZER_PATTERNS_H
#define ICUBE_BUZZER_PATTERNS_H
/* Active-high GPIO buzzer: alternating ON/OFF durations in milliseconds.
 * Recognizable, short patterns inspired by the supplied WAV recordings.
 * No WAV playback, PWM, pitch control, or audio output is used.
 * Duplicate swipes deliberately remain silent.
 */
struct buzzer_pattern {const unsigned short *durations;unsigned length;};
/* IN: two light confirmations, 195 ms total. */
static const unsigned short buzz_ok_durations[] = {55, 55, 85};
static const struct buzzer_pattern buzz_ok = {buzz_ok_durations, sizeof buzz_ok_durations / sizeof buzz_ok_durations[0]};
/* OUT: long-short-long, 425 ms total. */
static const unsigned short buzz_out_durations[] = {130, 55, 55, 55, 130};
static const struct buzzer_pattern buzz_out = {buzz_out_durations, sizeof buzz_out_durations / sizeof buzz_out_durations[0]};
/* LATE IN: double blip then longer warning, 475 ms total. */
static const unsigned short buzz_late_durations[] = {65, 50, 65, 135, 160};
static const struct buzzer_pattern buzz_late = {buzz_late_durations, sizeof buzz_late_durations / sizeof buzz_late_durations[0]};
/* ERROR: three equal pulses, 495 ms total. */
static const unsigned short buzz_fail_durations[] = {115, 75, 115, 75, 115};
static const struct buzzer_pattern buzz_fail = {buzz_fail_durations, sizeof buzz_fail_durations / sizeof buzz_fail_durations[0]};
/* Manager: three short acknowledgments followed by a long finish, 500 ms. */
static const unsigned short buzz_manager_durations[] = {65, 45, 65, 45, 65, 45, 170};
static const struct buzzer_pattern buzz_manager = {buzz_manager_durations, sizeof buzz_manager_durations / sizeof buzz_manager_durations[0]};
/* Communication reminder: one brief pulse, distinct from all swipe patterns.
   Active buzzer volume/pitch are fixed by the hardware. */
static const unsigned short buzz_comms_durations[] = {40};
static const struct buzzer_pattern buzz_comms = {buzz_comms_durations, 1};
#endif
