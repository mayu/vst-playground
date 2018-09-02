/*
 ==============================================================================
 
 This file was auto-generated!
 
 It contains the basic framework code for a JUCE plugin processor.
 
 ==============================================================================
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;


using namespace boost::asio;
using boost::asio::ip::udp;
using boost::asio::ip::address;


int fs = 0;
int chunk_size = 0;
long counter = 0;
std::queue<std::vector<float>> data;

std::queue<MidiBuffer> midi_buffers_out;
std::mutex midi_buffers_out_mutex;


client *wsclient_local = NULL;
client::connection_ptr wsclient_connection = NULL;
std::mutex g_data_mutex;

std::mutex g_midi_data_out_mutex;
std::queue<std::vector<uint8_t>> g_midi_data_out;



int min(int a, int b) {
    if (a < b) return a;
    return b;
}

// This message handler will be invoked once for each incoming message. It
// prints the message and then sends a copy of the message back to the server.
void on_message(client* c, websocketpp::connection_hdl hdl, message_ptr msg) {
    // Get the payload in bytes
    auto payload = msg->get_payload();
    auto level = 0.0000305185f;

    int16_t *payload_16bit = (int16_t*)&(payload[0]);
    long samples = payload.size() / (2*sizeof(int16_t));
    
//    std::cout << "MSG " << counter++ << ", " << samples << " samples arrived" << std::endl;
    
    // Fill in the data buffer
    std::vector<float> v;
    v.resize(samples);
    float sum = 0;
    for (int i = 0; i < samples; i++) {
        v[i] = level*payload_16bit[i<<1];
        sum += v[i];
    }
    if (sum != 0) {
        g_data_mutex.lock();
        data.push(v);
        g_data_mutex.unlock();

    }

}


void run_wsclient() {
    // Initialize ASIO
    std::string uri = "ws://localhost:9002";
    
    // Set message handler
    wsclient_local->set_message_handler(bind(&on_message, wsclient_local, ::_1, ::_2));
    
    // Get connection
    websocketpp::lib::error_code ec;
    wsclient_connection = wsclient_local->get_connection(uri, ec);
    wsclient_local->connect(wsclient_connection);
    wsclient_local->run();
}

void run_midi_reporter() {

    MidiMessage midi_message;
    int sample_number;

    while(1) {
        std::lock_guard<std::mutex> guard(midi_buffers_out_mutex);

        while( midi_buffers_out.size() >0 ) {
            MidiBuffer::Iterator midi_buffer_iter(midi_buffers_out.front());
            midi_buffers_out.pop();
            while(midi_buffer_iter.getNextEvent(midi_message, sample_number)) {
                std::cout << "Sample : " << sample_number << std::endl;
                try {
                    if (wsclient_local != NULL && wsclient_connection != NULL) {
                        wsclient_local->send(wsclient_connection, midi_message.getRawData(), midi_message.getRawDataSize(), websocketpp::frame::opcode::binary);
                    }
                } catch(std::exception const &e) {
                    std::cout << e.what() << std::endl;
                } catch(websocketpp::exception const &e) {
                    std::cout << e.what() << std::endl;
                } catch(...) {
                    std::cout << "other exception" << std::endl;
                }
            }
        }
    }
}

//==============================================================================
WaveSynthAudioProcessor::WaveSynthAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
: AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
                  .withInput  ("Input",  AudioChannelSet::stereo(), true)
#endif
                  .withOutput ("Output", AudioChannelSet::stereo(), true)
#endif
                  )
#endif
{
}

WaveSynthAudioProcessor::~WaveSynthAudioProcessor()
{

}

//==============================================================================
const String WaveSynthAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool WaveSynthAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool WaveSynthAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool WaveSynthAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double WaveSynthAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int WaveSynthAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
    // so this should be at least 1, even if you're not really implementing programs.
}

int WaveSynthAudioProcessor::getCurrentProgram()
{
    return 0;
}

void WaveSynthAudioProcessor::setCurrentProgram (int index)
{
}

const String WaveSynthAudioProcessor::getProgramName (int index)
{
    return {};
}

void WaveSynthAudioProcessor::changeProgramName (int index, const String& newName)
{
}


void send_request() {
    char textBuf[64];
    memset(textBuf, 0, sizeof(textBuf));
    sprintf(textBuf, "%d,%d,%d", 1, fs, chunk_size);
    
    if (wsclient_local != NULL && wsclient_connection != NULL) {
        wsclient_local->send(wsclient_connection, textBuf, strlen(textBuf), websocketpp::frame::opcode::text);
    }
}

//==============================================================================
void WaveSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    
    wsclient.init_asio();
    
    wsclient_local = &(this->wsclient);
    wsclient_thread = new std::thread(run_wsclient);
    midi_reporting_task = new std::thread(run_midi_reporter);
    
    fs = sampleRate;
    chunk_size = samplesPerBlock;
    //send_request();
}

void WaveSynthAudioProcessor::releaseResources()
{
/*
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    if (wsclient_local != NULL) {
        wsclient_local->stop();
    }
    wsclient_thread->join();// join();
*/
    
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool WaveSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
#if JucePlugin_IsMidiEffect
    ignoreUnused (layouts);
    return true;
#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;
    
    // This checks if the input layout matches the output layout
#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
#endif
    
    return true;
#endif
}
#endif

void WaveSynthAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    
    
    if(!midiMessages.isEmpty()) {
        std::lock_guard<std::mutex> guard(midi_buffers_out_mutex);
        midi_buffers_out.push(  MidiBuffer(midiMessages)  );
    }

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
   
    //    std::vector<
    int numSamples = buffer.getNumSamples();
    
    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    
//    std::cout << "Buffer num samples: " <<numSamples << std::endl;
    
    auto* channelData = buffer.getWritePointer(0);
    int samplesLeft = numSamples;
    int sampleIndex = 0;
    
    while(samplesLeft > 0) {
        
        // Check if we have anything to write
        if (data.size() == 0) {
            return;
        }
        //std::cout << "Queue size " << data.size() << std::endl;
        
        g_data_mutex.lock();
        std::vector<float> v = data.front();   // get the front of the queue
        g_data_mutex.unlock();
        if (samplesLeft >= v.size()) {
            // copying the entirety of v, popping
            g_data_mutex.lock();
            data.pop();
            g_data_mutex.unlock();
            
            memcpy((float*)&channelData[sampleIndex], (float*)&v[0], sizeof(float)*v.size());
            
//            for(int i=0; i<v.size(); i++) {
//                channelData[sampleIndex++] = v[i];
//            }
            sampleIndex += v.size();
            samplesLeft -= v.size();
        } else {
            for(int i=0; i<v.size(); i++) {
                if(i<samplesLeft) {
                    channelData[sampleIndex++] = v[i];
                } else {
                    v[i-samplesLeft] = v[i];
                }
            }
            v.resize(v.size()-samplesLeft);
            g_data_mutex.lock();
            data.front().swap(v);
            g_data_mutex.unlock();
            samplesLeft = 0;
        }
    }
    
//    std::lock_guard<std::mutex> guard_data(g_data_mutex);
//    while(data.size() > 10000) data.pop();

}

//==============================================================================
bool WaveSynthAudioProcessor::hasEditor() const
{
//    return false;
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* WaveSynthAudioProcessor::createEditor()
{
//    return NULL;
    return new WaveSynthAudioProcessorEditor (*this);
}

//==============================================================================
void WaveSynthAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void WaveSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new WaveSynthAudioProcessor();
}
