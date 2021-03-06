///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2018 Edouard Griffiths, F4EXB                                   //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#ifndef PLUGINS_SAMPLESOURCE_SOAPYSDRINPUT_SOAPYSDRINPUTTHREAD_H_
#define PLUGINS_SAMPLESOURCE_SOAPYSDRINPUT_SOAPYSDRINPUTTHREAD_H_

// SoapySDR is a device wrapper with a single stream supporting one or many Rx
// Therefore only one thread can be allocated for the Rx side
// All FIFOs must be registered before calling startWork()

#include <QThread>
#include <QMutex>
#include <QWaitCondition>

#include <SoapySDR/Device.hpp>

#include "soapysdr/devicesoapysdrshared.h"
#include "dsp/decimators.h"
#include "dsp/decimatorsfi.h"

class SampleSinkFifo;

class SoapySDRInputThread : public QThread {
    Q_OBJECT

public:
    SoapySDRInputThread(SoapySDR::Device* dev, unsigned int nbRxChannels, QObject* parent = 0);
    ~SoapySDRInputThread();

    void startWork();
    void stopWork();
    bool isRunning() const { return m_running; }
    unsigned int getNbChannels() const { return m_nbChannels; }
    void setLog2Decimation(unsigned int channel, unsigned int log2_decim);
    unsigned int getLog2Decimation(unsigned int channel) const;
    void setSampleRate(unsigned int sampleRate) { m_sampleRate = sampleRate; }
    unsigned int getSampleRate() const { return m_sampleRate; }
    void setFcPos(unsigned int channel, int fcPos);
    int getFcPos(unsigned int channel) const;
    void setFifo(unsigned int channel, SampleSinkFifo *sampleFifo);
    SampleSinkFifo *getFifo(unsigned int channel);

private:
    struct Channel
    {
        SampleVector m_convertBuffer;
        SampleSinkFifo* m_sampleFifo;
        unsigned int m_log2Decim;
        int m_fcPos;
        Decimators<qint32, qint8, SDR_RX_SAMP_SZ, 8> m_decimators8;
        Decimators<qint32, qint16, SDR_RX_SAMP_SZ, 12> m_decimators12;
        Decimators<qint32, qint16, SDR_RX_SAMP_SZ, 16> m_decimators16;
        DecimatorsFI m_decimatorsFloat;

        Channel() :
            m_sampleFifo(0),
            m_log2Decim(0),
            m_fcPos(0)
        {}

        ~Channel()
        {}
    };

    enum DecimatorType
    {
        Decimator8,
        Decimator12,
        Decimator16,
        DecimatorFloat
    };

    QMutex m_startWaitMutex;
    QWaitCondition m_startWaiter;
    bool m_running;
    SoapySDR::Device* m_dev;

    Channel *m_channels; //!< Array of channels dynamically allocated for the given number of Rx channels
    unsigned int m_sampleRate;
    unsigned int m_nbChannels;
    DecimatorType m_decimatorType;

    void run();
    unsigned int getNbFifos();
    void callbackSI8(const qint8* buf, qint32 len, unsigned int channel = 0);
    void callbackSI12(const qint16* buf, qint32 len, unsigned int channel = 0);
    void callbackSI16(const qint16* buf, qint32 len, unsigned int channel = 0);
    void callbackSIF(const float* buf, qint32 len, unsigned int channel = 0);
    void callbackMI(std::vector<void *>& buffs, qint32 samplesPerChannel);
};



#endif /* PLUGINS_SAMPLESOURCE_SOAPYSDRINPUT_SOAPYSDRINPUTTHREAD_H_ */
