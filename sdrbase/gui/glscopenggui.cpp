///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2017 F4EXB                                                      //
// written by Edouard Griffiths                                                  //
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

#include "glscopenggui.h"
#include "glscopeng.h"
#include "ui_glscopenggui.h"
#include "util/simpleserializer.h"

const double GLScopeNGGUI::amps[11] = { 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001, 0.0005, 0.0002, 0.0001 };

GLScopeNGGUI::GLScopeNGGUI(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::GLScopeNGGUI),
    m_messageQueue(0),
    m_glScope(0),
    m_scopeVis(0),
    m_sampleRate(0),
    m_traceLenMult(1),
    m_timeBase(1),
    m_timeOffset(0)
{
    qDebug("GLScopeNGGUI::GLScopeNGGUI");
    setEnabled(false);
    ui->setupUi(this);
}

GLScopeNGGUI::~GLScopeNGGUI()
{
    delete ui;
}

void GLScopeNGGUI::setBuddies(MessageQueue* messageQueue, ScopeVisNG* scopeVis, GLScopeNG* glScope)
{
    qDebug("GLScopeNGGUI::setBuddies");

    m_messageQueue = messageQueue;
    m_scopeVis = scopeVis;
    m_glScope = glScope;

    // initialize display combo
    ui->onlyX->setChecked(true);
    ui->onlyY->setChecked(false);
    ui->horizontalXY->setChecked(false);
    ui->verticalXY->setChecked(false);
    ui->polar->setChecked(false);
    m_glScope->setDisplayMode(GLScopeNG::DisplayX);

    // initialize trigger combo
    ui->trigPos->setChecked(true);
    ui->trigNeg->setChecked(false);
    ui->trigBoth->setChecked(false);
    ui->freerun->setChecked(true);

    // Add a trace
    ScopeVisNG::TraceData traceData;
    fillTraceData(traceData);
    m_scopeVis->addTrace(traceData);

    // Add a trigger
    ScopeVisNG::TriggerData triggerData;
    fillTriggerData(triggerData);
    m_scopeVis->addTrigger(triggerData);

    setEnabled(true);
    connect(m_glScope, SIGNAL(sampleRateChanged(int)), this, SLOT(on_scope_sampleRateChanged(int)));

    ui->traceMode->clear();
    fillProjectionCombo(ui->traceMode);

    ui->trigMode->clear();
    fillProjectionCombo(ui->trigMode);

    m_scopeVis->configure(2*m_traceLenMult*ScopeVisNG::m_traceChunkSize, m_timeOffset*10);
    m_scopeVis->configure(m_traceLenMult*ScopeVisNG::m_traceChunkSize, m_timeOffset*10);

    setTraceLenDisplay();
    setTimeScaleDisplay();
    setTimeOfsDisplay();
    setAmpScaleDisplay();
}

void GLScopeNGGUI::setSampleRate(int sampleRate)
{
    m_sampleRate = sampleRate;
}

void GLScopeNGGUI::on_scope_sampleRateChanged(int sampleRate)
{
    //m_sampleRate = m_glScope->getSampleRate();
    m_sampleRate = sampleRate;
    ui->sampleRateText->setText(tr("%1\nkS/s").arg(m_sampleRate / 1000.0f, 0, 'f', 2));
    setTraceLenDisplay();
    setTimeScaleDisplay();
    setTimeOfsDisplay();
}

void GLScopeNGGUI::resetToDefaults()
{
}


QByteArray GLScopeNGGUI::serialize() const
{
    // TODO
    SimpleSerializer s(1);
    return s.final();
}

bool GLScopeNGGUI::deserialize(const QByteArray& data)
{
    // TODO
    SimpleDeserializer d(data);

    if(!d.isValid()) {
        resetToDefaults();
        return false;
    }

    if(d.getVersion() == 1) {
        return true;
    } else {
        resetToDefaults();
        return false;
    }
}

void GLScopeNGGUI::on_onlyX_toggled(bool checked)
{
    if (checked)
    {
        ui->onlyY->setChecked(false);
        ui->horizontalXY->setChecked(false);
        ui->verticalXY->setChecked(false);
        ui->polar->setChecked(false);
        m_glScope->setDisplayMode(GLScopeNG::DisplayX);
    }
}

void GLScopeNGGUI::on_onlyY_toggled(bool checked)
{
    if (checked)
    {
        ui->onlyX->setChecked(false);
        ui->horizontalXY->setChecked(false);
        ui->verticalXY->setChecked(false);
        ui->polar->setChecked(false);
        m_glScope->setDisplayMode(GLScopeNG::DisplayY);
    }
}

void GLScopeNGGUI::on_horizontalXY_toggled(bool checked)
{
    if (checked)
    {
        ui->onlyX->setChecked(false);
        ui->onlyY->setChecked(false);
        ui->verticalXY->setChecked(false);
        ui->polar->setChecked(false);
        m_glScope->setDisplayMode(GLScopeNG::DisplayXYH);
    }
}

void GLScopeNGGUI::on_verticalXY_toggled(bool checked)
{
    if (checked)
    {
        ui->onlyX->setChecked(false);
        ui->onlyY->setChecked(false);
        ui->horizontalXY->setChecked(false);
        ui->polar->setChecked(false);
        m_glScope->setDisplayMode(GLScopeNG::DisplayXYV);
    }
}

void GLScopeNGGUI::on_polar_toggled(bool checked)
{
    if (checked)
    {
        ui->onlyX->setChecked(false);
        ui->onlyY->setChecked(false);
        ui->horizontalXY->setChecked(false);
        ui->verticalXY->setChecked(false);
        m_glScope->setDisplayMode(GLScopeNG::DisplayPol);
    }
}

void GLScopeNGGUI::on_traceIntensity_valueChanged(int value)
{
    ui->traceIntensity->setToolTip(QString("Trace intensity: %1").arg(value));
    m_glScope->setDisplayTraceIntensity(value);
}

void GLScopeNGGUI::on_gridIntensity_valueChanged(int value)
{
    ui->gridIntensity->setToolTip(QString("Grid intensity: %1").arg(value));
    m_glScope->setDisplayGridIntensity(value);
}

void GLScopeNGGUI::on_time_valueChanged(int value)
{
    m_timeBase = value;
    setTimeScaleDisplay();
    m_glScope->setTimeBase(m_timeBase);
}

void GLScopeNGGUI::on_timeOfs_valueChanged(int value)
{
    if ((value < 0) || (value > 100)) {
        return;
    }

    m_timeOffset = value;
    setTimeOfsDisplay();
    m_scopeVis->configure(m_traceLenMult*ScopeVisNG::m_traceChunkSize, m_timeOffset*10);
}

void GLScopeNGGUI::on_traceLen_valueChanged(int value)
{
    if ((value < 1) || (value > 100)) {
        return;
    }

    m_traceLenMult = value;
    m_scopeVis->configure(m_traceLenMult*ScopeVisNG::m_traceChunkSize, m_timeOffset*10);
    setTraceLenDisplay();
    setTimeScaleDisplay();
    setTimeOfsDisplay();
}

void GLScopeNGGUI::on_traceMode_currentIndexChanged(int index)
{
    setAmpScaleDisplay();
    changeCurrentTrace();
}

void GLScopeNGGUI::on_amp_valueChanged(int value)
{
    setAmpScaleDisplay();
    changeCurrentTrace();
}

void GLScopeNGGUI::setTimeScaleDisplay()
{
    m_sampleRate = m_glScope->getSampleRate();
    double t = (m_glScope->getTraceSize() * 1.0 / m_sampleRate) / (double) m_timeBase;

    if(t < 0.000001)
    {
        t = round(t * 100000000000.0) / 100.0;
        ui->timeText->setText(tr("%1\nns").arg(t));
    }
    else if(t < 0.001)
    {
        t = round(t * 100000000.0) / 100.0;
        ui->timeText->setText(tr("%1\nµs").arg(t));
    }
    else if(t < 1.0)
    {
        t = round(t * 100000.0) / 100.0;
        ui->timeText->setText(tr("%1\nms").arg(t));
    }
    else
    {
        t = round(t * 100.0) / 100.0;
        ui->timeText->setText(tr("%1\ns").arg(t));
    }
}

void GLScopeNGGUI::setTraceLenDisplay()
{
    uint n_samples = m_traceLenMult * ScopeVisNG::m_traceChunkSize;

    if (n_samples < 1000) {
        ui->traceLenText->setToolTip(tr("%1S").arg(n_samples));
    } else if (n_samples < 1000000) {
        ui->traceLenText->setToolTip(tr("%1kS").arg(n_samples/1000.0));
    } else {
        ui->traceLenText->setToolTip(tr("%1MS").arg(n_samples/1000000.0));
    }

    m_sampleRate = m_glScope->getSampleRate();
    qreal t = (m_glScope->getTraceSize() * 1.0 / m_sampleRate);

    if(t < 0.000001)
        ui->traceLenText->setText(tr("%1\nns").arg(t * 1000000000.0));
    else if(t < 0.001)
        ui->traceLenText->setText(tr("%1\nµs").arg(t * 1000000.0));
    else if(t < 1.0)
        ui->traceLenText->setText(tr("%1\nms").arg(t * 1000.0));
    else
        ui->traceLenText->setText(tr("%1\ns").arg(t * 1.0));
}

void GLScopeNGGUI::setTimeOfsDisplay()
{
    qreal dt = m_glScope->getTraceSize() * (m_timeOffset/100.0) / m_sampleRate;

    if(dt < 0.000001)
        ui->timeOfsText->setText(tr("%1\nns").arg(dt * 1000000000.0));
    else if(dt < 0.001)
        ui->timeOfsText->setText(tr("%1\nµs").arg(dt * 1000000.0));
    else if(dt < 1.0)
        ui->timeOfsText->setText(tr("%1\nms").arg(dt * 1000.0));
    else
        ui->timeOfsText->setText(tr("%1\ns").arg(dt * 1.0));
}

void GLScopeNGGUI::setAmpScaleDisplay()
{
    ScopeVisNG::ProjectionType projectionType = (ScopeVisNG::ProjectionType) ui->traceMode->currentIndex();
    float ampValue = amps[ui->amp->value()];

    if (projectionType == ScopeVisNG::ProjectionMagDB)
    {
        float displayValue = ampValue*500.0f;

        if (displayValue < 10.0f) {
            ui->ampText->setText(tr("%1\ndB").arg(displayValue, 0, 'f', 2));
        }
        else {
            ui->ampText->setText(tr("%1\ndB").arg(displayValue, 0, 'f', 1));
        }
    }
    else
    {
        qreal a = ampValue*10.0f;

        if(a < 0.000001)
            ui->ampText->setText(tr("%1\nn").arg(a * 1000000000.0));
        else if(a < 0.001)
            ui->ampText->setText(tr("%1\nµ").arg(a * 1000000.0));
        else if(a < 1.0)
            ui->ampText->setText(tr("%1\nm").arg(a * 1000.0));
        else
            ui->ampText->setText(tr("%1").arg(a * 1.0));
    }
}

void GLScopeNGGUI::changeCurrentTrace()
{
    ScopeVisNG::TraceData traceData;
    fillTraceData(traceData);
    uint32_t currentTraceIndex = ui->trace->value();
    m_scopeVis->changeTrace(traceData, currentTraceIndex);
}

void GLScopeNGGUI::fillProjectionCombo(QComboBox* comboBox)
{
    comboBox->addItem("Real", ScopeVisNG::ProjectionReal);
    comboBox->addItem("Imag", ScopeVisNG::ProjectionImag);
    comboBox->addItem("Mag", ScopeVisNG::ProjectionMagLin);
    comboBox->addItem("MagdB", ScopeVisNG::ProjectionMagDB);
    comboBox->addItem("Phi", ScopeVisNG::ProjectionPhase);
    comboBox->addItem("dPhi", ScopeVisNG::ProjectionDPhase);
}

void GLScopeNGGUI::fillTraceData(ScopeVisNG::TraceData& traceData)
{
    traceData.m_projectionType = (ScopeVisNG::ProjectionType) ui->traceMode->currentIndex();
    traceData.m_inputIndex = 0;
    traceData.m_amp = 0.2 / amps[ui->amp->value()];
    traceData.m_ofs = (10.0 * ui->ofsCoarse->value()) + (ui->ofsFine->value() / 20.0);
    traceData.m_traceDelay = 0;
}

void GLScopeNGGUI::fillTriggerData(ScopeVisNG::TriggerData& triggerData)
{
    triggerData.m_projectionType = (ScopeVisNG::ProjectionType) ui->traceMode->currentIndex();
    triggerData.m_inputIndex = 0;
    triggerData.m_triggerLevel = (ui->trigLevelCoarse->value() / 100.0) + (ui->trigLevelFine->value() / 20000.0);
    triggerData.m_triggerPositiveEdge = ui->trigPos->isChecked();
    triggerData.m_triggerBothEdges = ui->trigBoth->isChecked();
    triggerData.m_triggerDelay = ui->trigDelay->value();
    triggerData.m_triggerRepeat = ui->trigCount->value();
}

void GLScopeNGGUI::applySettings()
{
}

bool GLScopeNGGUI::handleMessage(Message* message)
{
    return false;
}
