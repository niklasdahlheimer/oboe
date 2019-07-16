/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <math.h>
#include "IntegerRatio.h"
#include "PolyphaseResampler.h"

using namespace flowgraph;

PolyphaseResampler::PolyphaseResampler(int32_t numTaps,
        int32_t inputRate,
        int32_t outputRate,
        int32_t channelCount)
        : MultiChannelResampler(numTaps, channelCount)
        {
    assert((numTaps % 4) == 0); // Required for loop unrolling.
    generateCoefficients(inputRate, outputRate);
}

// Generate coefficients in the order they will be used by readFrame().
// This is more complicated but readFrame() is called repeatedly and should be optimized.
void PolyphaseResampler::generateCoefficients(int32_t inputRate, int32_t outputRate) {
    {
        IntegerRatio ratio(inputRate, outputRate);
        ratio.reduce();
        mNumerator = ratio.getNumerator();
        mDenominator = ratio.getDenominator();
        mIntegerPhase = mDenominator;
        mCoefficients.resize(getNumTaps() * ratio.getDenominator());
    }
    int cursor = 0;
    double phase = 0.0;
    double phaseIncrement = (double) inputRate / (double) outputRate;
    const int spread = getNumTaps() / 2; // numTaps must be even.
    for (int i = 0; i < mDenominator; i++) {
        float tapPhase = phase - spread;
        for (int tap = 0; tap < getNumTaps(); tap++) {
            float radians = tapPhase * M_PI;
            mCoefficients.at(cursor++) = calculateWindowedSinc(radians, spread);
            tapPhase += 1.0;
        }
        phase += phaseIncrement;
        while (phase >= 1.0) {
            phase -= 1.0;
        }
    }
}

void PolyphaseResampler::readFrame(float *frame) {
    // Clear accumulator for mix.
    for (int channel = 0; channel < getChannelCount(); channel++) {
        mSingleFrame[channel] = 0.0;
    }

    float *coefficients = &mCoefficients[mCoefficientCursor];
    // Multiply input times windowed sinc function.
    int xIndex = (mCursor + mNumTaps) * getChannelCount();
    for (int i = 0; i < mNumTaps; i++) {
        float coefficient = *coefficients++;
        float *xFrame = &mX[xIndex];
        for (int channel = 0; channel < getChannelCount(); channel++) {
            mSingleFrame[channel] += coefficient * xFrame[channel];
        }
        xIndex -= getChannelCount();
    }

    mCoefficientCursor = (mCoefficientCursor + mNumTaps) % mCoefficients.size();

    // Copy accumulator to output.
    for (int channel = 0; channel < getChannelCount(); channel++) {
        frame[channel] = mSingleFrame[channel];
    }
}
