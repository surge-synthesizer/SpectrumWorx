////////////////////////////////////////////////////////////////////////////////
///
/// \file sample.hpp
/// ----------------
///
///   The external audio file the side channel can be fed from, instead of the
/// host's side chain port.
///
/// \note There were three files here: this one, a Windows `doLoad` over
/// DirectShow filter graphs, and a macOS one over `ExtAudioFile` and `FSRef`.
/// Both were platform decoders written in 2010 because JUCE 2 had none worth
/// having; JUCE 8 does, so there is one `doLoad` over `juce::AudioFormatManager`
/// and no platform arm at all.
///
/// Copyright (c) 2010 - 2016. Little Endian Ltd.
/// Copyright (c) 2026 the SpectrumWorx contributors.
/// SPDX-License-Identifier: GPL-3.0-or-later
///
////////////////////////////////////////////////////////////////////////////////
//------------------------------------------------------------------------------
#ifndef sample_hpp__A94590F9_6645_4380_8512_060CF57872FA
#define sample_hpp__A94590F9_6645_4380_8512_060CF57872FA
//------------------------------------------------------------------------------
#include "le/utility/span.hpp"

/// \note `juce::String` for supportedFormats(), which is a wildcard filter for a
/// `juce::FileChooser` rather than anything to do with a path. The paths
/// themselves are `fs::path`; \see io/jucePath.hpp.
#include <juce_core/juce_core.h>

#include "filesystem/import.h"

#include <memory>
#include <vector>
//------------------------------------------------------------------------------

namespace LE
{

////////////////////////////////////////////////////////////////////////////////
///
/// \class Sample
///
/// \brief Loads and streams a sample from memory, looped, always stereo.
///
////////////////////////////////////////////////////////////////////////////////

class Sample
{
  public:
    using ChannelData = LE::Utility::Span<float const>;

    /// Always, whatever the file holds: a mono file is duplicated and a wider
    /// one has its first two channels taken.
    static constexpr unsigned int numberOfChannels{2};

    ////////////////////////////////////////////////////////////////////////////
    // The factory samples.
    //
    // \note Compiled into the binary (assets/CMakeLists.txt), so they are known
    // by file name rather than by path -- which is also the only name a preset
    // can carry that survives being opened on another machine. The path built
    // from one is deliberately not a path to anything: one component, unrooted.
    // load() looks on disk first and falls back to the embedded set by name,
    // which is what the 2016 build did with <install>/Samples.
    //
    // \note This is the one thing `fs::path` models that `juce::File` had to be
    // talked into: a bare name needed `createFileWithoutCheckingPath()` to stop
    // JUCE resolving it against the working directory, and the resulting object
    // was a File that was not a file. A relative path is just a relative path.
    ////////////////////////////////////////////////////////////////////////////

    static std::vector<fs::path> factorySamples();
    static bool isFactorySample(fs::path const &);

  public:
    /// \param desiredSampleRate what to resample to, or zero for the file's own
    ///        -- which is what a caller that does not know the engine's rate yet
    ///        passes, so that a load is deferred rather than refused.
    ///
    /// \return nullptr on success, else a message fit for a dialog.
    ///
    /// \note It took a critical section, held while the decoded data was swapped
    /// into a Sample the audio thread might be reading. Nothing reads a Sample
    /// while it is being loaded any more: the caller decodes into one of its own
    /// and publishes it, and the one it displaces is destroyed on the main
    /// thread. See doc/tech/threading_model.md §5.
    char const *load(fs::path const &sampleFile, unsigned int desiredSampleRate);

    void clear();

    ChannelData channel(unsigned int index) const;

    ChannelData channel1() const;
    ChannelData channel2() const;

    unsigned int &samplePosition();
    unsigned int samplePosition() const { return samplePosition_; }
    void restart();

    fs::path const &sampleFile() const { return sampleFile_; }

    /// The rate the data was resampled to, i.e. the engine's at the time of the
    /// load. Zero when nothing is loaded.
    unsigned int sampleRate() const { return sampleRate_; }

    static juce::String supportedFormats();

    explicit operator bool() const;

  private:
    struct DataHolder
    {
        bool recreate(std::size_t newSizeInSamplesPerChannel);

        void takeDataFrom(DataHolder &other);

        std::unique_ptr<float[]> pBuffer;
        float *pChannel1End;
        float *pChannel2Beginning;
        float *pChannel2End;
    };

  private:
    static char const *doLoad(fs::path const &, unsigned int desiredSampleRate, DataHolder &);

  private:
    DataHolder data_;

    unsigned int samplePosition_{0};
    unsigned int sampleRate_{0};

    fs::path sampleFile_;

    static unsigned int const fixedNumberOfChannels = 2;
}; // class Sample

} // namespace LE

#endif // sample_hpp
