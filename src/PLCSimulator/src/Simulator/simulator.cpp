#include "simulator.h"

#include <QFile>
#include "../../lib/qjson/src/parser.h"

#include "ns3/plc.h"
#include "ns3/log.h"
#include "ns3/abort.h"

#include <ns3/simulator.h>

#include <QApplication>
#include <QSharedMemory>
#include <QTextStream>

#include <QDebug>

#include "../../lib/QCustomPlot/qcustomplot.h"
#include "../BodeWidget/bodewidget.h"

#include "../SimulatorMainWindow/simulatormainwindow.h"

void PLCSimulator::exportTransferData(QString fileName, Ptr<PLC_TransferBase> ctf){
    QFile file("./data/" + fileName + ".dat");
    file.open(QIODevice::Truncate | QIODevice::WriteOnly);

    Ptr<PLC_TimeVariantTransferVector> timeVariantCtf;
    Ptr<PLC_TransferVector> freqVariantCtf;

    bool ctfIsTimeVariant = ctf->IsTimeVariant();

    unsigned int timeLoops = 1;
    unsigned int freqLoops = 1;

    if(ctfIsTimeVariant){
        timeVariantCtf = StaticCast<PLC_TimeVariantTransferVector, PLC_TransferBase>(ctf);
        timeLoops = timeVariantCtf->GetNumTimeSlots();
        freqLoops = timeVariantCtf->GetNumBands();

        qDebug() << "Outter Dims: " << timeVariantCtf->GetValuesRef()->size();
        qDebug() << " Inner Dims: " << timeVariantCtf->GetValuesRef()->at(0).size();
    }
    else {
        freqVariantCtf = StaticCast<PLC_TransferVector, PLC_TransferBase>(ctf);
        freqLoops = freqVariantCtf->GetNumBands();
        qDebug() << "Freq Dims: " << freqVariantCtf->GetValuesRef()->size();
    }

    QTextStream stream(&file);
    stream.setRealNumberNotation(QTextStream::ScientificNotation);
    stream.setRealNumberPrecision(10);

    Ptr<const SpectrumModel> sm = ctf->GetSpectrumModel();
    stream << (*(sm->Begin())).fc << ',';
    stream << (*(sm->End() - 1)).fc << ',';
    stream << sm->GetNumBands() << '\n';

    stream << PLC_Time::GetPeriodS() << ',';
    stream << PLC_Time::GetNumTimeslots() << '\n';

    for(unsigned int i = 0; i < timeLoops; i++){
        PLC_ValueSpectrum* currentSpectrum = ctfIsTimeVariant ? &timeVariantCtf->GetValuesRef()->at(i):freqVariantCtf->GetValuesRef();

        for(unsigned int j = 0; j < freqLoops - 1; j++){
            stream << currentSpectrum->at(j).real() << ',';
        }
        stream << currentSpectrum->at(freqLoops - 1).real() << '\n';
    }

    for(unsigned int i = 0; i < timeLoops; i++){
        PLC_ValueSpectrum* currentSpectrum = ctfIsTimeVariant ? &timeVariantCtf->GetValuesRef()->at(i):freqVariantCtf->GetValuesRef();

        for(unsigned int j = 0; j < freqLoops - 1; j++){
            stream << currentSpectrum->at(j).imag() << ',';
        }

        stream << currentSpectrum->at(freqLoops - 1).imag() << '\n';
    }

    stream.flush();
    file.close();
}

QVector<QVector<double> > PLCSimulator::ctfToPlottable(Ptr<PLC_TransferVector> ctf){
    QVector<double> xValues;
    QVector<double> abs;
    QVector<double> arg;

    Ptr<const SpectrumModel> sm = ctf->GetSpectrumModel();

    QVector<QVector<double> > ret;

    Bands::const_iterator it = sm->Begin();

    qDebug() << "Collecting frequency axis values";

    for(it = it; it != sm->End(); it++){
        xValues.push_back(it->fc/1.0e6);
    }

    qDebug() << "Added: " << xValues.size() << "X-Coordinates";

    PLC_ValueSpectrum vals = ctf->GetValues();

    qDebug() << "Num y vals in ValueSpectrum.... " << vals.size();
    PLC_ValueSpectrum::iterator v_it;

    qDebug() << "Collecting y-axis Values";
    for(unsigned int i = 0; i < vals.size(); i++){
        double mag = std::abs(vals.at(i));

        if(mag != 0){
            abs.push_back(20.0*std::log10(mag));
            arg.push_back(std::arg(vals.at(i)));
        } else {
            xValues.remove(i);
        }
    }

    ret.push_back(xValues);
    ret.push_back(abs);
    ret.push_back(arg);

    return ret;
}

PLCSimulator::PLCSimulator(){
    QSharedMemory *sharedMemory = new QSharedMemory("PLC_TOPOLOGY_JSON_DATA");
    sharedMemory->attach();

    sharedMemory->lock();
    QByteArray jsonData((const char*)sharedMemory->constData(), sharedMemory->size());
    sharedMemory->unlock();
    sharedMemory->detach();
    delete(sharedMemory);

    QJson::Parser parser;

    QVariantMap modelData = parser.parse(jsonData).toMap();
    PLCTopologyModel* topologyModel = new PLCTopologyModel(modelData);
    this->loader = new PLCTopologyLoader(*topologyModel);

}

PLCSimulator::PLCSimulator(QString modelFileName){
    QFile modelFile(modelFileName);
    modelFile.open(QIODevice::ReadOnly);

    QByteArray jsonData = modelFile.readAll();
    QJson::Parser parser;

    QVariantMap modelData = parser.parse(jsonData).toMap();
    PLCTopologyModel* topologyModel = new PLCTopologyModel(modelData);
    this->loader = new PLCTopologyLoader(*topologyModel);
}

void PLCSimulator::showTransferFunctions(){
    //LogComponentEnable("PLC_Channel", LOG_LEVEL_FUNCTION);
    //LogComponentEnable("PLC_Edge", LOG_LEVEL_FUNCTION);
    //LogComponentEnable("PLC_Node", LOG_LEVEL_FUNCTION);

    // Calculate Channels PULLED DIRECTLY FROM PLC-CHANNEL-TEST
    Ptr<PLC_Channel> channel = loader->getChannel();


    channel->InitTransmissionChannels();

    PLC_NodeList netDeviceNodes = loader->getNetDeviceNodes();
    QStringList netDeviceNames = loader->getNetDeviceNames();

    PLC_NetdeviceList netDevices = loader->getNetDevices();

    PLC_NetdeviceList receiverDevices = loader->getReceivers();
    QStringList receiverNames = loader->getReceiverNames();

    PLC_NetdeviceList transmitterDevices = loader->getTransmitters();
    QStringList transmitterNames = loader->getTransmitterNames();


    Ptr<PLC_TxInterface> txIf;
    Ptr<PLC_RxInterface> rxIf;

    SimulatorMainWindow* mainWindow = new SimulatorMainWindow();
    BodeWidget * bodeWidget = mainWindow->plot;

    for(unsigned int i = 0; i < transmitterDevices.size(); i++){

        for(unsigned int j = 0; j< receiverDevices.size(); j++){
            if(transmitterDevices.at(i) != receiverDevices.at(j)){

                txIf = transmitterDevices.at(i)->GetHalfDuplexPhy()->GetTxInterface();
                rxIf = receiverDevices.at(j)->GetHalfDuplexPhy()->GetRxInterface();

                transmitterDevices.at(i)->GetChannelTransferImpl((receiverDevices.at(j)))->CalculateChannelTransferVector();

                PLC_ChannelTransferImpl *chImpl = txIf->GetChannelTransferImpl(PeekPointer(rxIf));
                NS_ABORT_MSG_IF(chImpl == NULL, "no channel from rxDev to txDev");

                Ptr<PLC_TransferBase> chTransFunc = chImpl->GetChannelTransferVector();

                QString name;
                name += transmitterNames.at(i) + "-to-" + receiverNames.at(j);

                NS_ASSERT(chTransFunc->GetValueType() == PLC_ValueBase::FREQ_SELECTIVE);

                if(chTransFunc->IsTimeVariant()){
                    qDebug() << "Cannot plot time variant functions. Exporting to" << (name + ".dat");
                    exportTransferData(name, chTransFunc);
                }
                else{
                    //Plot the channel transfer functionnew BodeWidget();
                    Ptr<PLC_TransferVector> data = StaticCast<PLC_TransferVector, PLC_TransferBase> (chTransFunc);
                    BodeData plotData = ctfToPlottable(data);

                    qDebug() << "Adding: " << name << "to Bode Widget";
                    bodeWidget->addBodePlot(&plotData, name);
                }
            }
        }
    }


    mainWindow->setWindowTitle("Channel Transfer Functions");
    mainWindow->resize(600, 400);
    bodeWidget->replot();
    mainWindow->show();


    qDebug() << "Diagram has " << transmitterDevices.size() << "Tx and " << receiverDevices.size() << "receivers";
}
