//=========================================================================
//  CNULLMESSAGEPROT.H - part of
//
//                  OMNeT++/OMNEST
//           Discrete System Simulation in C++
//
//  Author: Andras Varga, 2003
//          Dept. of Electrical and Computer Systems Engineering,
//          Monash University, Melbourne, Australia
//
//=========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 2003-2008 Andras Varga
  Copyright (C) 2006-2008 OpenSim Ltd.

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#ifndef __CNULLMESSAGEPROT_H__
#define __CNULLMESSAGEPROT_H__

#include "cparsimprotocolbase.h"
#include "cmessage.h"  // MK_PARSIM_BEGIN

NAMESPACE_BEGIN

// forward declarations
class cCommBuffer;
class cNMPLookahead;


/**
 * Implements the "null message algorithm".
 * Lookahead calculation is encapsulated into a separate object,
 * subclassed from cNMPLookahead.
 *
 * @ingroup Parsim
 */
class SIM_API cNullMessageProtocol : public cParsimProtocolBase
{
  protected:
    struct PartitionInfo
    {
        cMessage *eitEvent;  // EIT received from partition
        cMessage *eotEvent;  // events which marks that a null message should be sent out
        simtime_t lastEotSent; // last EOT value that was sent
    };

    // partition information
    int numSeg;              // number of partitions
    PartitionInfo *segInfo;  // partition info array, size numSeg

    // controls null message resend frequency, 0<=laziness<=1
    double laziness;

    // internally used message kinds
    enum
    {
        MK_PARSIM_EIT =       MK_PARSIM_BEGIN - 1,
        MK_PARSIM_RESENDEOT = MK_PARSIM_BEGIN - 2
    };

    bool debug;

    cNMPLookahead *lookaheadcalc;

  protected:
    // process buffers coming from other partitions
    virtual void processReceivedBuffer(cCommBuffer *buffer, int tag, int sourceProcId);

    // processes a received EIT: reschedule partition's EIT message
    virtual void processReceivedEIT(int sourceProcId, simtime_t eit);

    // resend null message to this partition
    virtual void sendNullMessage(int procId, simtime_t now);

    // reschedule event in FES, to the given time
    virtual void rescheduleEvent(cMessage *msg, simtime_t t);

  public:
    /**
     * Constructor.
     */
    cNullMessageProtocol();

    /**
     * Destructor.
     */
    virtual ~cNullMessageProtocol();

    /**
     * Redefined beacause we have to pass the same data to the lookahead calculator object
     * (cNMPLookahead) too.
     */
    virtual void setContext(cSimulation *sim, cParsimPartition *seg, cParsimCommunications *co);

    /**
     * Sets null message resend laziness. Value is between 0 and 1 --
     * 0.0 means eager resend, 1.0 means lazy resend.
     *
     * (Probably should never be 1.0, otherwise floating point rounding errors
     * may cause obscure EIT-deadlocks on receiving side.)
     */
    void setLaziness(double d)  {laziness = d;}

    /**
     * Get laziness of null message resend.
     */
    double getLaziness()  {return laziness;}

    /**
     * Called at the beginning of a simulation run.
     */
    virtual void startRun();

    /**
     * Called at the end of a simulation run.
     */
    virtual void endRun();

    /**
     * Scheduler function. The null message algorithm is embedded here.
     */
    virtual cMessage *getNextEvent();

    /**
     * In addition to its normal task (sending out the cMessage to the
     * given partition), it also does lookahead calculation and optional
     * piggybacking of null message on the cMessage.
     */
    virtual void processOutgoingMessage(cMessage *msg, int procId, int moduleId, int gateId, void *data);
};

NAMESPACE_END


#endif
