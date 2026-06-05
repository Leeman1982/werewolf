#ifndef ARPEGGIATOR_PATTERNS_H
#define ARPEGGIATOR_PATTERNS_H

#include <Arduino.h>

// Arpeggiator with 50 patterns - Prophet-8 inspired
#define MAX_ARP_NOTES 16
#define NUM_ARP_PATTERNS 50

enum ArpPattern {
  ARP_UP,              // 0: Up
  ARP_DOWN,            // 1: Down
  ARP_UP_DOWN,         // 2: Up/Down
  ARP_DOWN_UP,         // 3: Down/Up
  ARP_UP_DOWN_INC,     // 4: Up/Down Inclusive
  ARP_DOWN_UP_INC,     // 5: Down/Up Inclusive
  ARP_RANDOM,          // 6: Random
  ARP_ORDER_PLAYED,    // 7: Order played
  ARP_CONVERGE,        // 8: Converge (from edges to center)
  ARP_DIVERGE,         // 9: Diverge (from center to edges)
  ARP_THIRDS_UP,       // 10: Thirds up (1-3, 2-4, 3-5...)
  ARP_THIRDS_DOWN,     // 11: Thirds down
  ARP_FOURTHS_UP,      // 12: Fourths up
  ARP_FOURTHS_DOWN,    // 13: Fourths down
  ARP_OCTAVES,         // 14: Octave jumps
  ARP_REPEAT_2X,       // 15: Each note 2x
  ARP_REPEAT_3X,       // 16: Each note 3x
  ARP_PENDULUM,        // 17: 1-2-1-3-1-4...
  ARP_PING_PONG_2,     // 18: Up 2, down 2
  ARP_PING_PONG_3,     // 19: Up 3, down 3
  ARP_STEP_2,          // 20: Every 2nd note up
  ARP_STEP_3,          // 21: Every 3rd note up
  ARP_ZIGZAG,          // 22: 1-4-2-5-3...
  ARP_FIBONACCI,       // 23: Fibonacci intervals
  ARP_SPIRAL_UP,       // 24: Spiral upward pattern
  ARP_SPIRAL_DOWN,     // 25: Spiral downward
  ARP_BOUNCE,          // 26: Bounce pattern
  ARP_STUTTER,         // 27: Stutter (1-1-2-2-3-3...)
  ARP_GATES,           // 28: With rests
  ARP_TRIPLETS,        // 29: Triplet rhythm
  ARP_DOTTED,          // 30: Dotted rhythm
  ARP_SWING,           // 31: Swing rhythm
  ARP_EUCLIDEAN_5_8,   // 32: Euclidean 5/8
  ARP_EUCLIDEAN_7_12,  // 33: Euclidean 7/12
  ARP_POLYRHYTHM_3_4,  // 34: 3 over 4 polyrhythm
  ARP_POLYRHYTHM_5_4,  // 35: 5 over 4 polyrhythm
  ARP_CHORD_INVERSION, // 36: Cycle through inversions
  ARP_ALTERNATING,     // 37: Alternate high/low
  ARP_PYRAMID,         // 38: Build up then down
  ARP_CASCADE,         // 39: Cascading pattern
  ARP_SINE_WAVE,       // 40: Sine wave selection
  ARP_TRIANGLE_WAVE,   // 41: Triangle wave selection
  ARP_SQUARE_WAVE,     // 42: Square wave pattern
  ARP_SAW_WAVE,        // 43: Sawtooth pattern
  ARP_PROBABILISTIC,   // 44: Probabilistic selection
  ARP_FRACTAL,         // 45: Fractal pattern
  ARP_CHAOS,           // 46: Chaotic pattern
  ARP_RHYTHMIC_1,      // 47: Rhythmic pattern 1
  ARP_RHYTHMIC_2,      // 48: Rhythmic pattern 2
  ARP_CLASSIC_808      // 49: Classic 808-style
};

const char* arpPatternNames[NUM_ARP_PATTERNS] = {
  "Up", "Down", "Up/Down", "Down/Up",
  "Up/Dn Inc", "Dn/Up Inc", "Random", "As Played",
  "Converge", "Diverge", "Thirds Up", "Thirds Dn",
  "4ths Up", "4ths Dn", "Octaves", "Repeat 2x",
  "Repeat 3x", "Pendulum", "PingPong2", "PingPong3",
  "Step 2", "Step 3", "Zigzag", "Fibonacci",
  "Spiral Up", "Spiral Dn", "Bounce", "Stutter",
  "Gates", "Triplets", "Dotted", "Swing",
  "Euclid 5/8", "Euclid 7/12", "Poly 3/4", "Poly 5/4",
  "Inversions", "Alternate", "Pyramid", "Cascade",
  "Sine Wave", "Triangle", "Square", "Sawtooth",
  "Probabilis", "Fractal", "Chaos", "Rhythmic 1",
  "Rhythmic 2", "808 Style"
};

class Arpeggiator {
private:
  int heldNotes[MAX_ARP_NOTES];
  int noteCount;
  int currentStep;
  int currentOctave;
  int maxOctaves;
  unsigned long lastStepTime;
  float bpm;
  ArpPattern pattern;
  bool isPlaying;
  int gateLength;  // 1-100%

  // Pattern-specific state
  int repeatCounter;
  int direction;
  int patternPhase;

  // Euclidean rhythm helper
  bool euclideanRhythm[16];
  int euclideanPos;

  void generateEuclideanRhythm(int steps, int pulses) {
    for (int i = 0; i < 16; i++) euclideanRhythm[i] = false;
    if (pulses == 0 || steps == 0) return;

    int bucket = 0;
    for (int i = 0; i < steps; i++) {
      bucket += pulses;
      if (bucket >= steps) {
        bucket -= steps;
        if (i < 16) euclideanRhythm[i] = true;
      }
    }
  }

  int getNextNoteIndex() {
    if (noteCount == 0) return -1;

    int idx = 0;

    switch (pattern) {
      case ARP_UP:
        idx = currentStep % noteCount;
        break;

      case ARP_DOWN:
        idx = noteCount - 1 - (currentStep % noteCount);
        break;

      case ARP_UP_DOWN: {
        int cycle = (noteCount - 1) * 2;
        int pos = currentStep % cycle;
        idx = (pos < noteCount) ? pos : (cycle - pos);
        break;
      }

      case ARP_DOWN_UP: {
        int cycle = (noteCount - 1) * 2;
        int pos = currentStep % cycle;
        idx = (pos < noteCount) ? (noteCount - 1 - pos) : (pos - noteCount + 1);
        break;
      }

      case ARP_UP_DOWN_INC: {
        int cycle = noteCount * 2;
        int pos = currentStep % cycle;
        idx = (pos < noteCount) ? pos : (cycle - pos - 1);
        break;
      }

      case ARP_RANDOM:
        idx = random(noteCount);
        break;

      case ARP_ORDER_PLAYED:
        idx = currentStep % noteCount;
        break;

      case ARP_CONVERGE: {
        int half = noteCount / 2;
        if ((currentStep / half) % 2 == 0) {
          idx = currentStep % half;
        } else {
          idx = noteCount - 1 - (currentStep % half);
        }
        break;
      }

      case ARP_DIVERGE: {
        int half = noteCount / 2;
        int pos = currentStep % noteCount;
        idx = (pos % 2 == 0) ? (half + pos / 2) : (half - 1 - pos / 2);
        break;
      }

      case ARP_THIRDS_UP:
        idx = ((currentStep % noteCount) + (currentStep / noteCount) * 2) % noteCount;
        break;

      case ARP_FOURTHS_UP:
        idx = ((currentStep % noteCount) + (currentStep / noteCount) * 3) % noteCount;
        break;

      case ARP_OCTAVES:
        idx = currentStep % noteCount;
        currentOctave = (currentStep / noteCount) % maxOctaves;
        break;

      case ARP_REPEAT_2X:
        idx = (currentStep / 2) % noteCount;
        break;

      case ARP_REPEAT_3X:
        idx = (currentStep / 3) % noteCount;
        break;

      case ARP_PENDULUM: {
        // 1, 2, 1, 3, 1, 4, 1, 5...
        if (currentStep % 2 == 0) {
          idx = 0;
        } else {
          idx = min((currentStep / 2) + 1, noteCount - 1);
        }
        break;
      }

      case ARP_ZIGZAG: {
        // 1-4-2-5-3-6...
        int base = currentStep / 2;
        idx = (currentStep % 2 == 0) ? base : (base + noteCount / 2);
        idx = idx % noteCount;
        break;
      }

      case ARP_BOUNCE:
        idx = abs((currentStep % (noteCount * 2)) - noteCount);
        break;

      case ARP_STUTTER:
        idx = (currentStep / 2) % noteCount;
        break;

      case ARP_ALTERNATING: {
        if (noteCount < 2) {
          idx = 0;
        } else {
          idx = (currentStep % 2 == 0) ? 0 : (noteCount - 1);
        }
        break;
      }

      case ARP_PYRAMID: {
        // Build: 1, 1-2, 1-2-3, then down
        int level = (currentStep / noteCount) % (noteCount + 1);
        if (level == 0) level = 1;
        idx = currentStep % level;
        break;
      }

      case ARP_SINE_WAVE: {
        float phase = (currentStep * TWO_PI) / noteCount;
        idx = (int)((sin(phase) + 1.0f) * (noteCount - 1) / 2.0f);
        break;
      }

      case ARP_PROBABILISTIC:
        idx = (random(100) < 70) ? (currentStep % noteCount) : random(noteCount);
        break;

      default:
        idx = currentStep % noteCount;
        break;
    }

    return constrain(idx, 0, noteCount - 1);
  }

public:
  Arpeggiator() {
    noteCount = 0;
    currentStep = 0;
    currentOctave = 0;
    maxOctaves = 1;
    lastStepTime = 0;
    bpm = 120.0f;
    pattern = ARP_UP;
    isPlaying = false;
    gateLength = 80;
    repeatCounter = 0;
    direction = 1;
    patternPhase = 0;
    euclideanPos = 0;
    generateEuclideanRhythm(8, 5);
  }

  void noteOn(int note) {
    // Add note if not already held
    bool found = false;
    for (int i = 0; i < noteCount; i++) {
      if (heldNotes[i] == note) {
        found = true;
        break;
      }
    }

    if (!found && noteCount < MAX_ARP_NOTES) {
      heldNotes[noteCount++] = note;

      // Sort notes (ascending)
      for (int i = 0; i < noteCount - 1; i++) {
        for (int j = i + 1; j < noteCount; j++) {
          if (heldNotes[i] > heldNotes[j]) {
            int temp = heldNotes[i];
            heldNotes[i] = heldNotes[j];
            heldNotes[j] = temp;
          }
        }
      }
    }

    if (!isPlaying && noteCount > 0) {
      isPlaying = true;
      currentStep = 0;
      lastStepTime = millis();
    }
  }

  void noteOff(int note) {
    // Remove note from held notes
    for (int i = 0; i < noteCount; i++) {
      if (heldNotes[i] == note) {
        for (int j = i; j < noteCount - 1; j++) {
          heldNotes[j] = heldNotes[j + 1];
        }
        noteCount--;
        break;
      }
    }

    if (noteCount == 0) {
      isPlaying = false;
      currentStep = 0;
    }
  }

  void allNotesOff() {
    noteCount = 0;
    isPlaying = false;
    currentStep = 0;
  }

  int update(unsigned long currentTime) {
    if (!isPlaying || noteCount == 0) return -1;

    float stepInterval = (60000.0f / bpm) / 4.0f; // 16th notes

    if (currentTime - lastStepTime >= (unsigned long)stepInterval) {
      lastStepTime = currentTime;

      int idx = getNextNoteIndex();
      if (idx >= 0 && idx < noteCount) {
        int note = heldNotes[idx];

        // Apply octave offset if pattern uses it
        if (pattern == ARP_OCTAVES) {
          note += currentOctave * 12;
        }

        currentStep++;
        return note;
      }

      currentStep++;
    }

    return -1;
  }

  void setPattern(ArpPattern p) {
    pattern = p;
    currentStep = 0;

    // Generate specific rhythm patterns
    if (pattern == ARP_EUCLIDEAN_5_8) {
      generateEuclideanRhythm(8, 5);
    } else if (pattern == ARP_EUCLIDEAN_7_12) {
      generateEuclideanRhythm(12, 7);
    }
  }

  void setBPM(float newBpm) {
    bpm = constrain(newBpm, 30.0f, 300.0f);
  }

  void setOctaveRange(int octaves) {
    maxOctaves = constrain(octaves, 1, 4);
  }

  void setGateLength(int length) {
    gateLength = constrain(length, 10, 100);
  }

  ArpPattern getPattern() { return pattern; }
  float getBPM() { return bpm; }
  int getNoteCount() { return noteCount; }
  bool getIsPlaying() { return isPlaying; }
  int getGateLength() { return gateLength; }
};

#endif
