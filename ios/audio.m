#import <AVFoundation/AVFoundation.h>

#include <math.h>
#include <stdint.h>

static AVAudioPlayer *musicPlayer;
static AVAudioPlayer *sfxPlayer;
static int currentTrack = INT32_MIN;

static const uint8_t titleNotes[] = {
    11,18,25,20,16,18,20,20,47,47,35,30,32,32,42,42,47,47,35,30,32,32,42,42,
    30,25,28,32,30,25,37,30,35,37,44,42,35,37,30,32,47,47,35,30,32,32,42,42,
    47,47,35,30,32,32,42,42,42,35,25,8,40,32,28,42};
static const uint8_t climbNotes[] = {
    42,42,35,61,47,47,47,42,42,42,35,61,47,47,47,42,37,37,37,37,30,30,30,30,
    37,37,37,37,30,30,30,30,37,59,54,25,32,8,35,56,37,59,54,25,32,8,35,56,
    37,59,54,25,35,32,35,40,37,37,37,52,0,54,44,49};
static const uint8_t climbBNotes[] = {
    37,28,32,32,28,28,30,32,37,28,32,32,28,28,30,32,37,28,32,32,28,28,30,32,
    32,25,28,25,28,30,32,32,37,28,32,32,28,28,30,32,32,25,28,25,28,30,32,32,
    42,42,52,49,47,47,44,44,37,37,37,37,37,37,37,37,32,32,32,32,32,32,32,32};
static const uint8_t summitNotes[] = {
    51,50,43,16,13,21,47,52,51,50,43,16,13,21,47,52,
    51,50,43,16,13,21,47,52,49,44,43,49,44,42,47,44};
static const uint8_t completeNotes[] = {
    61,61,59,59,54,54,54,52,54,54,56,52,59,59,52,52};

static void append16(NSMutableData *data, uint16_t value) {
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    [data appendBytes:bytes length:2];
}

static void append32(NSMutableData *data, uint32_t value) {
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    [data appendBytes:bytes length:4];
}

static NSData *wavForNotes(const double *frequencies, size_t count,
                           double secondsPerNote, float volume) {
    const uint32_t rate = 22050;
    const uint32_t framesPerNote = (uint32_t)lrint(secondsPerNote * rate);
    const uint32_t frames = framesPerNote * (uint32_t)count;
    NSMutableData *wav = [NSMutableData dataWithCapacity:44 + frames * 2];
    [wav appendBytes:"RIFF" length:4]; append32(wav, 36 + frames * 2);
    [wav appendBytes:"WAVEfmt " length:8]; append32(wav, 16); append16(wav, 1);
    append16(wav, 1); append32(wav, rate); append32(wav, rate * 2);
    append16(wav, 2); append16(wav, 16);
    [wav appendBytes:"data" length:4]; append32(wav, frames * 2);

    for (size_t note = 0; note < count; note++) {
        double frequency = frequencies[note];
        for (uint32_t frame = 0; frame < framesPerNote; frame++) {
            double phase = frequency > 0 ? fmod((double)frame * frequency / rate, 1.0) : 0;
            double triangle = 1.0 - 4.0 * fabs(phase - 0.5);
            double edge = MIN(1.0, MIN(frame / (rate * 0.008),
                                      (framesPerNote - frame) / (rate * 0.035)));
            double gate = frame < framesPerNote * 0.82 ? edge : 0;
            int16_t sample = (int16_t)lrint(triangle * gate * volume * 32767.0);
            append16(wav, (uint16_t)sample);
        }
    }
    return wav;
}

static NSData *wavForPicoNotes(const uint8_t *notes, size_t count) {
    double *frequencies = calloc(count, sizeof(double));
    for (size_t i = 0; i < count; i++)
        frequencies[i] = notes[i] ? 32.70319566 * pow(2.0, (notes[i] - 1) / 12.0) : 0;
    NSData *data = wavForNotes(frequencies, count, 0.48, 0.20f);
    free(frequencies);
    return data;
}

static void setupSession(void) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        AVAudioSession *session = AVAudioSession.sharedInstance;
        [session setCategory:AVAudioSessionCategoryAmbient
                 withOptions:AVAudioSessionCategoryOptionMixWithOthers error:nil];
        [session setActive:YES error:nil];
    });
}

void audio_music(int track) {
    setupSession();
    if (track == currentTrack) return;
    currentTrack = track;
    [musicPlayer stop];
    musicPlayer = nil;

    const uint8_t *notes = NULL;
    size_t count = 0;
    switch (track) {
        case 0: notes = titleNotes; count = sizeof(titleNotes); break;
        case 10: notes = climbNotes; count = sizeof(climbNotes); break;
        case 20: notes = climbBNotes; count = sizeof(climbBNotes); break;
        case 30: notes = summitNotes; count = sizeof(summitNotes); break;
        case 40: notes = completeNotes; count = sizeof(completeNotes); break;
        default: return;
    }
    musicPlayer = [[AVAudioPlayer alloc] initWithData:wavForPicoNotes(notes, count) error:nil];
    musicPlayer.numberOfLoops = -1;
    musicPlayer.volume = 0.7f;
    [musicPlayer prepareToPlay];
    [musicPlayer play];
}

void audio_sfx(int effect) {
    setupSession();
    double notes[4] = {392.0, 0, 0, 0};
    size_t count = 1;
    double duration = 0.055;
    switch (effect) {
        case 1: case 2: case 3:
            notes[0] = 523.25; notes[1] = 659.25; count = 2; break;
        case 9: case 15: case 16:
            notes[0] = 1046.50; notes[1] = 783.99; count = 2; break;
        case 0: case 37: case 38:
            notes[0] = 329.63; notes[1] = 261.63; notes[2] = 196.0; notes[3] = 130.81;
            count = 4; duration = 0.09; break;
        case 13: case 14: case 23: case 51: case 54: case 55:
            notes[0] = 659.25; notes[1] = 783.99; notes[2] = 1046.50;
            count = 3; break;
        default: break;
    }
    [sfxPlayer stop];
    sfxPlayer = [[AVAudioPlayer alloc] initWithData:wavForNotes(notes, count, duration, 0.32f)
                                              error:nil];
    [sfxPlayer play];
}
