///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2015 Edouard Griffiths, F4EXB                                   //
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

#include <string.h>
#include <errno.h>

#include <QDebug>
#include <QNetworkReply>
#include <QBuffer>

#include "SWGDeviceSettings.h"
#include "SWGDeviceState.h"

#include "util/simpleserializer.h"
#include "dsp/dspcommands.h"
#include "dsp/dspengine.h"
#include "dsp/filerecord.h"
#include "device/devicesourceapi.h"
#include "device/devicesinkapi.h"
#include "hackrf/devicehackrfvalues.h"
#include "hackrf/devicehackrfshared.h"

#include "hackrfinput.h"
#include "hackrfinputthread.h"

MESSAGE_CLASS_DEFINITION(HackRFInput::MsgConfigureHackRF, Message)
MESSAGE_CLASS_DEFINITION(HackRFInput::MsgReportHackRF, Message)
MESSAGE_CLASS_DEFINITION(HackRFInput::MsgFileRecord, Message)
MESSAGE_CLASS_DEFINITION(HackRFInput::MsgStartStop, Message)

HackRFInput::HackRFInput(DeviceSourceAPI *deviceAPI) :
    m_deviceAPI(deviceAPI),
	m_settings(),
	m_dev(0),
	m_hackRFThread(0),
	m_deviceDescription("HackRF"),
	m_running(false)
{
    openDevice();

    m_fileSink = new FileRecord(QString("test_%1.sdriq").arg(m_deviceAPI->getDeviceUID()));
    m_deviceAPI->addSink(m_fileSink);

    m_deviceAPI->setBuddySharedPtr(&m_sharedParams);

    m_networkManager = new QNetworkAccessManager();
    connect(m_networkManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(networkManagerFinished(QNetworkReply*)));
}

HackRFInput::~HackRFInput()
{
    disconnect(m_networkManager, SIGNAL(finished(QNetworkReply*)), this, SLOT(networkManagerFinished(QNetworkReply*)));
    delete m_networkManager;

    if (m_running) {
        stop();
    }

    m_deviceAPI->removeSink(m_fileSink);
    delete m_fileSink;
    closeDevice();
	m_deviceAPI->setBuddySharedPtr(0);
}

void HackRFInput::destroy()
{
    delete this;
}

bool HackRFInput::openDevice()
{
    if (m_dev != 0)
    {
        closeDevice();
    }

    if (!m_sampleFifo.setSize(1<<19))
    {
        qCritical("HackRFInput::start: could not allocate SampleFifo");
        return false;
    }

    if (m_deviceAPI->getSinkBuddies().size() > 0)
    {
        DeviceSinkAPI *buddy = m_deviceAPI->getSinkBuddies()[0];
        DeviceHackRFParams *buddySharedParams = (DeviceHackRFParams *) buddy->getBuddySharedPtr();

        if (buddySharedParams == 0)
        {
            qCritical("HackRFInput::openDevice: could not get shared parameters from buddy");
            return false;
        }

        if (buddySharedParams->m_dev == 0) // device is not opened by buddy
        {
            qCritical("HackRFInput::openDevice: could not get HackRF handle from buddy");
            return false;
        }

        m_sharedParams = *(buddySharedParams); // copy parameters from buddy
        m_dev = m_sharedParams.m_dev;          // get HackRF handle
    }
    else
    {
        if ((m_dev = DeviceHackRF::open_hackrf(qPrintable(m_deviceAPI->getSampleSourceSerial()))) == 0)
        {
            qCritical("HackRFInput::openDevice: could not open HackRF %s", qPrintable(m_deviceAPI->getSampleSourceSerial()));
            return false;
        }

        m_sharedParams.m_dev = m_dev;
    }

    return true;
}

void HackRFInput::init()
{
    applySettings(m_settings, true);
}

bool HackRFInput::start()
{
//	QMutexLocker mutexLocker(&m_mutex);
    if (!m_dev) {
        return false;
    }

    if (m_running) stop();

    m_hackRFThread = new HackRFInputThread(m_dev, &m_sampleFifo);

//	mutexLocker.unlock();

	applySettings(m_settings, true);

	m_hackRFThread->setSamplerate(m_settings.m_devSampleRate);
	m_hackRFThread->setLog2Decimation(m_settings.m_log2Decim);
	m_hackRFThread->setFcPos((int) m_settings.m_fcPos);

	m_hackRFThread->startWork();

	qDebug("HackRFInput::startInput: started");
	m_running = true;

	return true;
}

void HackRFInput::closeDevice()
{
    if (m_deviceAPI->getSinkBuddies().size() == 0)
    {
        qDebug("HackRFInput::closeDevice: closing device since Tx side is not open");

        if(m_dev != 0) // close BladeRF
        {
            hackrf_close(m_dev);
            //hackrf_exit(); // TODO: this may not work if several HackRF Devices are running concurrently. It should be handled globally in the application
        }
    }

    m_sharedParams.m_dev = 0;
    m_dev = 0;
}

void HackRFInput::stop()
{
	qDebug("HackRFInput::stop");
//	QMutexLocker mutexLocker(&m_mutex);

	if (m_hackRFThread != 0)
	{
		m_hackRFThread->stopWork();
		delete m_hackRFThread;
		m_hackRFThread = 0;
	}

	m_running = false;
}

QByteArray HackRFInput::serialize() const
{
    return m_settings.serialize();
}

bool HackRFInput::deserialize(const QByteArray& data)
{
    bool success = true;

    if (!m_settings.deserialize(data))
    {
        m_settings.resetToDefaults();
        success = false;
    }

    MsgConfigureHackRF* message = MsgConfigureHackRF::create(m_settings, true);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureHackRF* messageToGUI = MsgConfigureHackRF::create(m_settings, true);
        m_guiMessageQueue->push(messageToGUI);
    }

    return success;
}

const QString& HackRFInput::getDeviceDescription() const
{
	return m_deviceDescription;
}

int HackRFInput::getSampleRate() const
{
	return (m_settings.m_devSampleRate / (1<<m_settings.m_log2Decim));
}

quint64 HackRFInput::getCenterFrequency() const
{
	return m_settings.m_centerFrequency;
}

void HackRFInput::setCenterFrequency(qint64 centerFrequency)
{
    HackRFInputSettings settings = m_settings;
    settings.m_centerFrequency = centerFrequency;

    MsgConfigureHackRF* message = MsgConfigureHackRF::create(settings, false);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue)
    {
        MsgConfigureHackRF* messageToGUI = MsgConfigureHackRF::create(settings, false);
        m_guiMessageQueue->push(messageToGUI);
    }
}

bool HackRFInput::handleMessage(const Message& message)
{
	if (MsgConfigureHackRF::match(message))
	{
		MsgConfigureHackRF& conf = (MsgConfigureHackRF&) message;
		qDebug() << "HackRFInput::handleMessage: MsgConfigureHackRF";

		bool success = applySettings(conf.getSettings(), conf.getForce());

		if (!success)
		{
			qDebug("HackRFInput::handleMessage: config error");
		}

		return true;
	}
    else if (MsgFileRecord::match(message))
    {
        MsgFileRecord& conf = (MsgFileRecord&) message;
        qDebug() << "HackRFInput::handleMessage: MsgFileRecord: " << conf.getStartStop();

        if (conf.getStartStop())
        {
            if (m_settings.m_fileRecordName.size() != 0) {
                m_fileSink->setFileName(m_settings.m_fileRecordName);
            } else {
                m_fileSink->genUniqueFileName(m_deviceAPI->getDeviceUID());
            }

            m_fileSink->startRecording();
        }
        else
        {
            m_fileSink->stopRecording();
        }

        return true;
    }
    else if (MsgStartStop::match(message))
    {
        MsgStartStop& cmd = (MsgStartStop&) message;
        qDebug() << "HackRFInput::handleMessage: MsgStartStop: " << (cmd.getStartStop() ? "start" : "stop");

        if (cmd.getStartStop())
        {
            if (m_deviceAPI->initAcquisition())
            {
                m_deviceAPI->startAcquisition();
            }
        }
        else
        {
            m_deviceAPI->stopAcquisition();
        }

        if (m_settings.m_useReverseAPI) {
            webapiReverseSendStartStop(cmd.getStartStop());
        }

        return true;
    }
	else
	{
		return false;
	}
}

void HackRFInput::setDeviceCenterFrequency(quint64 freq_hz)
{
	qint64 df = ((qint64)freq_hz * m_settings.m_LOppmTenths) / 10000000LL;
	freq_hz += df;

	hackrf_error rc = (hackrf_error) hackrf_set_freq(m_dev, static_cast<uint64_t>(freq_hz));

	if (rc != HACKRF_SUCCESS)
	{
		qWarning("HackRFInput::setDeviceCenterFrequency: could not frequency to %llu Hz", freq_hz);
	}
	else
	{
		qWarning("HackRFInput::setDeviceCenterFrequency: frequency set to %llu Hz", freq_hz);
	}
}

bool HackRFInput::applySettings(const HackRFInputSettings& settings, bool force)
{
//	QMutexLocker mutexLocker(&m_mutex);

	bool forwardChange = false;
	hackrf_error rc;
    QList<QString> reverseAPIKeys;

	qDebug() << "HackRFInput::applySettings";

    if ((m_settings.m_dcBlock != settings.m_dcBlock) || force) {
        reverseAPIKeys.append("dcBlock");
    }
    if ((m_settings.m_iqCorrection != settings.m_iqCorrection) || force) {
        reverseAPIKeys.append("iqCorrection");
    }
    if ((m_settings.m_linkTxFrequency != settings.m_linkTxFrequency) || force) {
        reverseAPIKeys.append("linkTxFrequency");
    }

	if ((m_settings.m_dcBlock != settings.m_dcBlock) ||
	    (m_settings.m_iqCorrection != settings.m_iqCorrection) || force)
	{
		m_deviceAPI->configureCorrections(settings.m_dcBlock, settings.m_iqCorrection);
	}

	if ((m_settings.m_devSampleRate != settings.m_devSampleRate) || force)
	{
        reverseAPIKeys.append("devSampleRate");
        forwardChange = true;

        if (m_dev != 0)
		{
	        rc = (hackrf_error) hackrf_set_sample_rate_manual(m_dev, settings.m_devSampleRate, 1);

			if (rc != HACKRF_SUCCESS)
			{
				qCritical("HackRFInput::applySettings: could not set sample rate TO %llu S/s: %s", settings.m_devSampleRate, hackrf_error_name(rc));
			}
			else
			{
			    if (m_hackRFThread != 0)
			    {
	                qDebug("HackRFInput::applySettings: sample rate set to %llu S/s", settings.m_devSampleRate);
	                m_hackRFThread->setSamplerate(settings.m_devSampleRate);
			    }
			}
		}
	}

	if ((m_settings.m_log2Decim != settings.m_log2Decim) || force)
	{
        reverseAPIKeys.append("log2Decim");
		forwardChange = true;

		if (m_hackRFThread != 0)
		{
			m_hackRFThread->setLog2Decimation(settings.m_log2Decim);
			qDebug() << "HackRFInput: set decimation to " << (1<<settings.m_log2Decim);
		}
	}

	if (force || (m_settings.m_centerFrequency != settings.m_centerFrequency)) // forward delta to buddy if necessary
	{
        reverseAPIKeys.append("centerFrequency");

        if (m_settings.m_linkTxFrequency && (m_deviceAPI->getSinkBuddies().size() > 0))
	    {
	        DeviceSinkAPI *buddy = m_deviceAPI->getSinkBuddies()[0];
            DeviceHackRFShared::MsgConfigureFrequencyDelta *deltaMsg = DeviceHackRFShared::MsgConfigureFrequencyDelta::create(
                    settings.m_centerFrequency - m_settings.m_centerFrequency);

	        if (buddy->getSampleSinkGUIMessageQueue())
	        {
	            DeviceHackRFShared::MsgConfigureFrequencyDelta *deltaMsgToGUI = new DeviceHackRFShared::MsgConfigureFrequencyDelta(*deltaMsg);
                buddy->getSampleSinkGUIMessageQueue()->push(deltaMsgToGUI);
	        }

	        buddy->getSampleSinkInputMessageQueue()->push(deltaMsg);
	    }
	}

    if ((m_settings.m_LOppmTenths != settings.m_LOppmTenths) || force) {
        reverseAPIKeys.append("LOppmTenths");
    }
    if ((m_settings.m_fcPos != settings.m_fcPos) || force) {
        reverseAPIKeys.append("fcPos");
    }

	if ((m_settings.m_centerFrequency != settings.m_centerFrequency) ||
	    (m_settings.m_devSampleRate != settings.m_devSampleRate) ||
        (m_settings.m_LOppmTenths != settings.m_LOppmTenths) ||
        (m_settings.m_log2Decim != settings.m_log2Decim) ||
        (m_settings.m_fcPos != settings.m_fcPos) || force)
	{
        qint64 deviceCenterFrequency = DeviceSampleSource::calculateDeviceCenterFrequency(
                settings.m_centerFrequency,
                0,
                settings.m_log2Decim,
                (DeviceSampleSource::fcPos_t) settings.m_fcPos,
                settings.m_devSampleRate);

		if (m_dev != 0) {
			setDeviceCenterFrequency(deviceCenterFrequency);
		}

		forwardChange = true;
	}

	if ((m_settings.m_fcPos != settings.m_fcPos) || force)
	{
		if (m_hackRFThread != 0)
		{
			m_hackRFThread->setFcPos((int) settings.m_fcPos);
			qDebug() << "HackRFInput: set fc pos (enum) to " << (int) settings.m_fcPos;
		}
	}

	if ((m_settings.m_lnaGain != settings.m_lnaGain) || force)
	{
        reverseAPIKeys.append("lnaGain");

		if (m_dev != 0)
		{
			rc = (hackrf_error) hackrf_set_lna_gain(m_dev, settings.m_lnaGain);

			if (rc != HACKRF_SUCCESS) {
				qDebug("HackRFInput::applySettings: airspy_set_lna_gain failed: %s", hackrf_error_name(rc));
			} else {
				qDebug() << "HackRFInput:applySettings: LNA gain set to " << settings.m_lnaGain;
			}
		}
	}

	if ((m_settings.m_vgaGain != settings.m_vgaGain) || force)
	{
        reverseAPIKeys.append("vgaGain");

		if (m_dev != 0)
		{
			rc = (hackrf_error) hackrf_set_vga_gain(m_dev, settings.m_vgaGain);

			if (rc != HACKRF_SUCCESS) {
				qDebug("HackRFInput::applySettings: hackrf_set_vga_gain failed: %s", hackrf_error_name(rc));
			} else {
				qDebug() << "HackRFInput:applySettings: VGA gain set to " << settings.m_vgaGain;
			}
		}
	}

	if ((m_settings.m_bandwidth != settings.m_bandwidth) || force)
	{
        reverseAPIKeys.append("bandwidth");

        if (m_dev != 0)
		{
	        uint32_t bw_index = hackrf_compute_baseband_filter_bw_round_down_lt(settings.m_bandwidth + 1); // +1 so the round down to lower than yields desired bandwidth
			rc = (hackrf_error) hackrf_set_baseband_filter_bandwidth(m_dev, bw_index);

			if (rc != HACKRF_SUCCESS) {
				qDebug("HackRFInput::applySettings: hackrf_set_baseband_filter_bandwidth failed: %s", hackrf_error_name(rc));
			} else {
				qDebug() << "HackRFInput:applySettings: Baseband BW filter set to " << settings.m_bandwidth << " Hz";
			}
		}
	}

	if ((m_settings.m_biasT != settings.m_biasT) || force)
	{
        reverseAPIKeys.append("biasT");

		if (m_dev != 0)
		{
			rc = (hackrf_error) hackrf_set_antenna_enable(m_dev, (settings.m_biasT ? 1 : 0));

			if (rc != HACKRF_SUCCESS) {
				qDebug("HackRFInput::applySettings: hackrf_set_antenna_enable failed: %s", hackrf_error_name(rc));
			} else {
				qDebug() << "HackRFInput:applySettings: bias tee set to " << settings.m_biasT;
			}
		}
	}

	if ((m_settings.m_lnaExt != settings.m_lnaExt) || force)
	{
        reverseAPIKeys.append("lnaExt");

		if (m_dev != 0)
		{
			rc = (hackrf_error) hackrf_set_amp_enable(m_dev, (settings.m_lnaExt ? 1 : 0));

			if (rc != HACKRF_SUCCESS) {
				qDebug("HackRFInput::applySettings: hackrf_set_amp_enable failed: %s", hackrf_error_name(rc));
			} else {
				qDebug() << "HackRFInput:applySettings: extra LNA set to " << settings.m_lnaExt;
			}
		}
	}

	if (forwardChange)
	{
		int sampleRate = settings.m_devSampleRate/(1<<settings.m_log2Decim);
		DSPSignalNotification *notif = new DSPSignalNotification(sampleRate, settings.m_centerFrequency);
        m_fileSink->handleMessage(*notif); // forward to file sink
        m_deviceAPI->getDeviceEngineInputMessageQueue()->push(notif);
	}

    if (settings.m_useReverseAPI)
    {
        bool fullUpdate = ((m_settings.m_useReverseAPI != settings.m_useReverseAPI) && settings.m_useReverseAPI) ||
                (m_settings.m_reverseAPIAddress != settings.m_reverseAPIAddress) ||
                (m_settings.m_reverseAPIPort != settings.m_reverseAPIPort) ||
                (m_settings.m_reverseAPIDeviceIndex != settings.m_reverseAPIDeviceIndex);
        webapiReverseSendSettings(reverseAPIKeys, settings, fullUpdate || force);
    }

	m_settings = settings;

    qDebug() << "HackRFInput::applySettings: "
            << " m_centerFrequency: " << m_settings.m_centerFrequency << " Hz"
            << " m_LOppmTenths: " << m_settings.m_LOppmTenths
            << " m_bandwidth: " << m_settings.m_bandwidth
            << " m_lnaGain: " << m_settings.m_lnaGain
            << " m_vgaGain: " << m_settings.m_vgaGain
            << " m_log2Decim: " << m_settings.m_log2Decim
            << " m_fcPos: " << m_settings.m_fcPos
            << " m_devSampleRate: " << m_settings.m_devSampleRate
            << " m_biasT: " << m_settings.m_biasT
            << " m_lnaExt: " << m_settings.m_lnaExt
            << " m_dcBlock: " << m_settings.m_dcBlock
            << " m_linkTxFrequency: " << m_settings.m_linkTxFrequency;

	return true;
}

int HackRFInput::webapiSettingsGet(
                SWGSDRangel::SWGDeviceSettings& response,
                QString& errorMessage)
{
    (void) errorMessage;
    response.setHackRfInputSettings(new SWGSDRangel::SWGHackRFInputSettings());
    response.getHackRfInputSettings()->init();
    webapiFormatDeviceSettings(response, m_settings);
    return 200;
}

int HackRFInput::webapiSettingsPutPatch(
                bool force,
                const QStringList& deviceSettingsKeys,
                SWGSDRangel::SWGDeviceSettings& response, // query + response
                QString& errorMessage)
{
    (void) errorMessage;
    HackRFInputSettings settings = m_settings;

    if (deviceSettingsKeys.contains("centerFrequency")) {
        settings.m_centerFrequency = response.getHackRfInputSettings()->getCenterFrequency();
    }
    if (deviceSettingsKeys.contains("LOppmTenths")) {
        settings.m_LOppmTenths = response.getHackRfInputSettings()->getLOppmTenths();
    }
    if (deviceSettingsKeys.contains("bandwidth")) {
        settings.m_bandwidth = response.getHackRfInputSettings()->getBandwidth();
    }
    if (deviceSettingsKeys.contains("lnaGain")) {
        settings.m_lnaGain = response.getHackRfInputSettings()->getLnaGain();
    }
    if (deviceSettingsKeys.contains("vgaGain")) {
        settings.m_vgaGain = response.getHackRfInputSettings()->getVgaGain();
    }
    if (deviceSettingsKeys.contains("log2Decim")) {
        settings.m_log2Decim = response.getHackRfInputSettings()->getLog2Decim();
    }
    if (deviceSettingsKeys.contains("fcPos"))
    {
        int fcPos = response.getHackRfInputSettings()->getFcPos();
        fcPos = fcPos < 0 ? 0 : fcPos > 2 ? 2 : fcPos;
        settings.m_fcPos = (HackRFInputSettings::fcPos_t) fcPos;
    }
    if (deviceSettingsKeys.contains("devSampleRate")) {
        settings.m_devSampleRate = response.getHackRfInputSettings()->getDevSampleRate();
    }
    if (deviceSettingsKeys.contains("biasT")) {
        settings.m_biasT = response.getHackRfInputSettings()->getBiasT() != 0;
    }
    if (deviceSettingsKeys.contains("lnaExt")) {
        settings.m_lnaExt = response.getHackRfInputSettings()->getLnaExt() != 0;
    }
    if (deviceSettingsKeys.contains("dcBlock")) {
        settings.m_dcBlock = response.getHackRfInputSettings()->getDcBlock() != 0;
    }
    if (deviceSettingsKeys.contains("iqCorrection")) {
        settings.m_iqCorrection = response.getHackRfInputSettings()->getIqCorrection() != 0;
    }
    if (deviceSettingsKeys.contains("linkTxFrequency")) {
        settings.m_linkTxFrequency = response.getHackRfInputSettings()->getLinkTxFrequency() != 0;
    }
    if (deviceSettingsKeys.contains("fileRecordName")) {
        settings.m_fileRecordName = *response.getHackRfInputSettings()->getFileRecordName();
    }

    MsgConfigureHackRF *msg = MsgConfigureHackRF::create(settings, force);
    m_inputMessageQueue.push(msg);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgConfigureHackRF *msgToGUI = MsgConfigureHackRF::create(settings, force);
        m_guiMessageQueue->push(msgToGUI);
    }

    webapiFormatDeviceSettings(response, settings);
    return 200;
}

void HackRFInput::webapiFormatDeviceSettings(SWGSDRangel::SWGDeviceSettings& response, const HackRFInputSettings& settings)
{
    response.getHackRfInputSettings()->setCenterFrequency(settings.m_centerFrequency);
    response.getHackRfInputSettings()->setLOppmTenths(settings.m_LOppmTenths);
    response.getHackRfInputSettings()->setBandwidth(settings.m_bandwidth);
    response.getHackRfInputSettings()->setLnaGain(settings.m_lnaGain);
    response.getHackRfInputSettings()->setVgaGain(settings.m_vgaGain);
    response.getHackRfInputSettings()->setLog2Decim(settings.m_log2Decim);
    response.getHackRfInputSettings()->setFcPos(settings.m_fcPos);
    response.getHackRfInputSettings()->setDevSampleRate(settings.m_devSampleRate);
    response.getHackRfInputSettings()->setBiasT(settings.m_biasT ? 1 : 0);
    response.getHackRfInputSettings()->setLnaExt(settings.m_lnaExt ? 1 : 0);
    response.getHackRfInputSettings()->setDcBlock(settings.m_dcBlock ? 1 : 0);
    response.getHackRfInputSettings()->setIqCorrection(settings.m_iqCorrection ? 1 : 0);
    response.getHackRfInputSettings()->setLinkTxFrequency(settings.m_linkTxFrequency ? 1 : 0);

    if (response.getHackRfInputSettings()->getFileRecordName()) {
        *response.getHackRfInputSettings()->getFileRecordName() = settings.m_fileRecordName;
    } else {
        response.getHackRfInputSettings()->setFileRecordName(new QString(settings.m_fileRecordName));
    }
}

int HackRFInput::webapiRunGet(
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage)
{
    (void) errorMessage;
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    return 200;
}

int HackRFInput::webapiRun(
        bool run,
        SWGSDRangel::SWGDeviceState& response,
        QString& errorMessage)
{
    (void) errorMessage;
    m_deviceAPI->getDeviceEngineStateStr(*response.getState());
    MsgStartStop *message = MsgStartStop::create(run);
    m_inputMessageQueue.push(message);

    if (m_guiMessageQueue) // forward to GUI if any
    {
        MsgStartStop *msgToGUI = MsgStartStop::create(run);
        m_guiMessageQueue->push(msgToGUI);
    }

    return 200;
}

void HackRFInput::webapiReverseSendSettings(QList<QString>& deviceSettingsKeys, const HackRFInputSettings& settings, bool force)
{
    SWGSDRangel::SWGDeviceSettings *swgDeviceSettings = new SWGSDRangel::SWGDeviceSettings();
    swgDeviceSettings->setTx(0);
    swgDeviceSettings->setDeviceHwType(new QString("HackRF"));
    swgDeviceSettings->setHackRfInputSettings(new SWGSDRangel::SWGHackRFInputSettings());
    SWGSDRangel::SWGHackRFInputSettings *swgHackRFInputSettings = swgDeviceSettings->getHackRfInputSettings();

    // transfer data that has been modified. When force is on transfer all data except reverse API data

    if (deviceSettingsKeys.contains("centerFrequency") || force) {
        swgHackRFInputSettings->setCenterFrequency(settings.m_centerFrequency);
    }
    if (deviceSettingsKeys.contains("LOppmTenths") || force) {
        swgHackRFInputSettings->setLOppmTenths(settings.m_LOppmTenths);
    }
    if (deviceSettingsKeys.contains("bandwidth") || force) {
        swgHackRFInputSettings->setBandwidth(settings.m_bandwidth);
    }
    if (deviceSettingsKeys.contains("lnaGain") || force) {
        swgHackRFInputSettings->setLnaGain(settings.m_lnaGain);
    }
    if (deviceSettingsKeys.contains("vgaGain") || force) {
        swgHackRFInputSettings->setVgaGain(settings.m_vgaGain);
    }
    if (deviceSettingsKeys.contains("log2Decim") || force) {
        swgHackRFInputSettings->setLog2Decim(settings.m_log2Decim);
    }
    if (deviceSettingsKeys.contains("fcPos") || force) {
        swgHackRFInputSettings->setFcPos((int) settings.m_fcPos);
    }
    if (deviceSettingsKeys.contains("devSampleRate") || force) {
        swgHackRFInputSettings->setDevSampleRate(settings.m_devSampleRate);
    }
    if (deviceSettingsKeys.contains("biasT") || force) {
        swgHackRFInputSettings->setBiasT(settings.m_biasT ? 1 : 0);
    }
    if (deviceSettingsKeys.contains("lnaExt") || force) {
        swgHackRFInputSettings->setLnaExt(settings.m_lnaExt ? 1 : 0);
    }
    if (deviceSettingsKeys.contains("dcBlock") || force) {
        swgHackRFInputSettings->setDcBlock(settings.m_dcBlock ? 1 : 0);
    }
    if (deviceSettingsKeys.contains("iqCorrection") || force) {
        swgHackRFInputSettings->setIqCorrection(settings.m_iqCorrection ? 1 : 0);
    }
    if (deviceSettingsKeys.contains("linkTxFrequency") || force) {
        swgHackRFInputSettings->setLinkTxFrequency(settings.m_linkTxFrequency ? 1 : 0);
    }
    if (deviceSettingsKeys.contains("fileRecordName") || force) {
        swgHackRFInputSettings->setFileRecordName(new QString(settings.m_fileRecordName));
    }

    QString deviceSettingsURL = QString("http://%1:%2/sdrangel/deviceset/%3/device/settings")
            .arg(settings.m_reverseAPIAddress)
            .arg(settings.m_reverseAPIPort)
            .arg(settings.m_reverseAPIDeviceIndex);
    m_networkRequest.setUrl(QUrl(deviceSettingsURL));
    m_networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QBuffer *buffer=new QBuffer();
    buffer->open((QBuffer::ReadWrite));
    buffer->write(swgDeviceSettings->asJson().toUtf8());
    buffer->seek(0);

    // Always use PATCH to avoid passing reverse API settings
    m_networkManager->sendCustomRequest(m_networkRequest, "PATCH", buffer);

    delete swgDeviceSettings;
}

void HackRFInput::webapiReverseSendStartStop(bool start)
{
    QString deviceSettingsURL = QString("http://%1:%2/sdrangel/deviceset/%3/device/run")
            .arg(m_settings.m_reverseAPIAddress)
            .arg(m_settings.m_reverseAPIPort)
            .arg(m_settings.m_reverseAPIDeviceIndex);
    m_networkRequest.setUrl(QUrl(deviceSettingsURL));

    if (start) {
        m_networkManager->sendCustomRequest(m_networkRequest, "POST");
    } else {
        m_networkManager->sendCustomRequest(m_networkRequest, "DELETE");
    }
}

void HackRFInput::networkManagerFinished(QNetworkReply *reply)
{
    QNetworkReply::NetworkError replyError = reply->error();

    if (replyError)
    {
        qWarning() << "HackRFInput::networkManagerFinished:"
                << " error(" << (int) replyError
                << "): " << replyError
                << ": " << reply->errorString();
        return;
    }

    QString answer = reply->readAll();
    answer.chop(1); // remove last \n
    qDebug("HackRFInput::networkManagerFinished: reply:\n%s", answer.toStdString().c_str());
}
