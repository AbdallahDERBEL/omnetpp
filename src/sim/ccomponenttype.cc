//=========================================================================
//  CCOMPONENTTYPE.CC - part of
//
//                    OMNeT++/OMNEST
//             Discrete System Simulation in C++
//
//=========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2008 Andras Varga
  Copyright (C) 2006-2008 OpenSim Ltd.

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#include <string.h>
#include "ccomponenttype.h"
#include "cmodule.h"
#include "csimplemodule.h"
#include "ccompoundmodule.h"
#include "cchannel.h"
#include "cenvir.h"
#include "cparimpl.h"
#include "cexception.h"
#include "globals.h"
#include "cdelaychannel.h"
#include "cdataratechannel.h"
#include "cmodelchange.h"
#include "cproperties.h"
#include "cproperty.h"

#ifdef WITH_PARSIM
#include "ccommbuffer.h"
#include "parsim/cplaceholdermod.h"
#endif

USING_NAMESPACE

cComponentType::cComponentType(const char *qname) : cNoncopyableOwnedObject(qname,false)
{
    // store fully qualified name, and set name to simple (unqualified) name
    qualifiedName = qname;
    const char *lastDot = strrchr(qname, '.');
    setName(!lastDot ? qname : lastDot + 1);
    availabilityTested = available = false;
}

cComponentType::~cComponentType()
{
    for (StringToParMap::iterator it = sharedParMap.begin(); it!=sharedParMap.end(); ++it)
        delete it->second;
    for (ParImplSet::iterator it = sharedParSet.begin(); it!=sharedParSet.end(); ++it)
        delete *it;
}

cComponentType *cComponentType::find(const char *qname)
{
    return dynamic_cast<cComponentType *>(componentTypes.getInstance()->lookup(qname));
}

cComponentType *cComponentType::get(const char *qname)
{
    cComponentType *p = find(qname);
    if (!p) {
        const char *hint = (!qname || !strchr(qname,'.')) ? " (fully qualified type name expected)" : "";
        throw cRuntimeError("NED type \"%s\" not found%s", qname, hint);
    }
    return p;
}

cParImpl *cComponentType::getSharedParImpl(const char *key) const
{
    StringToParMap::const_iterator it = sharedParMap.find(key);
    return it==sharedParMap.end() ? NULL : it->second;
}

void cComponentType::putSharedParImpl(const char *key, cParImpl *value)
{
    ASSERT(sharedParMap.find(key)==sharedParMap.end()); // not yet in there
    value->setIsShared(true);
    sharedParMap[key] = value;
}

// cannot go inline due to declaration order
bool cComponentType::Less::operator()(cParImpl *a, cParImpl *b) const
{
    return a->compare(b) < 0;
}

cParImpl *cComponentType::getSharedParImpl(cParImpl *value) const
{
    ParImplSet::const_iterator it = sharedParSet.find(value);
    return it==sharedParSet.end() ? NULL : *it;
}

void cComponentType::putSharedParImpl(cParImpl *value)
{
    ASSERT(sharedParSet.find(value)==sharedParSet.end()); // not yet in there
    value->setIsShared(true);
    sharedParSet.insert(value);
}

bool cComponentType::isAvailable()
{
    if (!availabilityTested)
    {
        const char *className = getImplementationClassName();
        available = classes.getInstance()->lookup(className) != NULL;
        availabilityTested = true;
    }
    return available;
}

//----

cModuleType::cModuleType(const char *name) : cComponentType(name)
{
}

cModule *cModuleType::create(const char *modname, cModule *parentmod)
{
    return create(modname, parentmod, -1, 0);
}

cModule *cModuleType::create(const char *modname, cModule *parentmod, int vectorsize, int index)
{
    // notify pre-change listeners
    if (parentmod && parentmod->hasListeners(PRE_MODEL_CHANGE)) {
        cPreModuleAddNotification tmp;
        tmp.moduleType = this;
        tmp.moduleName = modname;
        tmp.parentModule = parentmod;
        tmp.vectorSize = vectorsize;
        tmp.index = index;
        parentmod->emit(PRE_MODEL_CHANGE, &tmp);
    }

    // set context type to "BUILD"
    cContextTypeSwitcher tmp(CTX_BUILD);

    // Object members of the new module class are collected to tmplist.
    cDefaultList tmplist;
    cDefaultList *oldlist = cOwnedObject::getDefaultOwner();
    cOwnedObject::setDefaultOwner(&tmplist);
    cModule *mod;
    try {
        // create the new module object
#ifdef WITH_PARSIM
        bool isLocal = ev.isModuleLocal(parentmod, modname, vectorsize<0 ? -1 : index);
        mod = isLocal ? createModuleObject() : new cPlaceholderModule();
#else
        mod = createModuleObject();
#endif
    }
    catch (std::exception& e) {
        // restore defaultowner, otherwise it'll remain pointing to a dead object
        cOwnedObject::setDefaultOwner(oldlist);
        throw;
    }

    // set up module: set parent, module type, name, vector size
    if (parentmod)
        parentmod->insertSubmodule(mod);
    mod->setComponentType(this);
    if (vectorsize < 0)
        mod->setName(modname);
    else
        mod->setNameAndIndex(modname, index, vectorsize);

    // set system module (must be done before takeAllObjectsFrom(tmplist) because
    // if parentmod==NULL, mod itself is on tmplist)
    if (!parentmod)
         simulation.setSystemModule(mod);

    // put the object members of the new module to their place
    mod->takeAllObjectsFrom(tmplist);

    // restore defaultowner (must precede parameters)
    cOwnedObject::setDefaultOwner(oldlist);

    // register with cSimulation
    int id = simulation.registerModule(mod);
    mod->setId(id);

    // set up RNG mapping
    ev.getRNGMappingFor(mod);

    // should be called before any gateCreated calls on this module
    EVCB.moduleCreated(mod);

    // add parameters and gates to the new module;
    // note: setupGateVectors() will be called from finalizeParameters()
    addParametersAndGatesTo(mod);

    // notify envir
    ev.configure(mod);

    // notify post-change listeners
    if (mod->hasListeners(POST_MODEL_CHANGE)) {
        cPostModuleAddNotification tmp;
        tmp.module = mod;
        mod->emit(POST_MODEL_CHANGE, &tmp);
    }

    // done -- if it's a compound module, buildInside() will do the rest
    return mod;
}

cModule *cModuleType::instantiateModuleClass(const char *classname)
{
    cObject *obj = cObjectFactory::createOne(classname); // this won't return NULL
    cModule *mod = dynamic_cast<cModule *>(obj);
    if (!mod)
        throw cRuntimeError("incorrect module class %s: not subclassed from cModule", classname);

    // check module object
    if (!mod->isModule())
        throw cRuntimeError("incorrect module class %s: isModule() returns false", classname);

    if (isSimple()) {
        if (dynamic_cast<cSimpleModule *>(mod)==NULL)
            throw cRuntimeError("incorrect simple module class %s: not subclassed from cSimpleModule", classname);
        if (mod->isSimple()==false)
            throw cRuntimeError("incorrect simple module class %s: isSimple() returns false", classname);
    }
    else {
        if (dynamic_cast<cCompoundModule *>(mod)==NULL)
            throw cRuntimeError("incorrect compound module class %s: not subclassed from cCompoundModule", classname);
        if (mod->isSimple()==true)
            throw cRuntimeError("incorrect compound module class %s: isSimple() returns true", classname);
    }

    return mod;
}

cModule *cModuleType::createScheduleInit(const char *modname, cModule *parentmod)
{
    if (!parentmod)
        throw cRuntimeError("createScheduleInit(): parent module pointer cannot be NULL "
                            "when creating module named '%s' of type %s", modname, getFullName());
    cModule *mod = create(modname, parentmod);
    mod->finalizeParameters();
    mod->buildInside();
    mod->scheduleStart(simulation.getSimTime());
    mod->callInitialize();
    return mod;
}

cModuleType *cModuleType::find(const char *qname)
{
    return dynamic_cast<cModuleType *>(componentTypes.getInstance()->lookup(qname));
}

cModuleType *cModuleType::get(const char *qname)
{
    cModuleType *p = find(qname);
    if (!p) {
        const char *hint = (!qname || !strchr(qname,'.')) ? " (fully qualified type name expected)" : "";
        throw cRuntimeError("NED module type \"%s\" not found%s", qname, hint);
    }
    return p;
}

//----

cChannelType *cChannelType::idealChannelType;
cChannelType *cChannelType::delayChannelType;
cChannelType *cChannelType::datarateChannelType;

cChannelType::cChannelType(const char *name) : cComponentType(name)
{
}

cChannel *cChannelType::instantiateChannelClass(const char *classname)
{
    cObject *obj = cObjectFactory::createOne(classname); // this won't return NULL
    cChannel *channel = dynamic_cast<cChannel *>(obj);
    if (!channel)
        throw cRuntimeError("class %s is not a channel type", classname); //FIXME better msg
    return channel;
}

cChannel *cChannelType::create(const char *name)
{
    cContextTypeSwitcher tmp(CTX_BUILD);

    // Object members of the new channel class are collected to tmplist.
    cDefaultList tmplist;
    cDefaultList *oldlist = cOwnedObject::getDefaultOwner();
    cOwnedObject::setDefaultOwner(&tmplist);

    // create channel object
    cChannel *channel;
    try {
        channel = createChannelObject();
    }
    catch (std::exception& e) {
        // restore defaultowner, otherwise it'll remain pointing to a dead object
        cOwnedObject::setDefaultOwner(oldlist);
        throw;
    }

    // determine channel name
    if (!name) {
        cProperty *prop = getProperties()->get("defaultname");
        if (prop)
            name = prop->getValue(cProperty::DEFAULTKEY);
        if (!name)
            name = "channel";
    }

    // set up channel: set name, channel type, etc
    channel->setName(name);
    channel->setComponentType(this);

    // put the object members of the new module to their place
    oldlist->take(channel);
    channel->takeAllObjectsFrom(tmplist);

    // restore defaultowner
    cOwnedObject::setDefaultOwner(oldlist);

    // set up RNG mapping
    ev.getRNGMappingFor(channel);

    // add parameters to the new module
    addParametersTo(channel);

    return channel;
}

cChannelType *cChannelType::find(const char *qname)
{
    return dynamic_cast<cChannelType *>(componentTypes.getInstance()->lookup(qname));
}

cChannelType *cChannelType::get(const char *qname)
{
    cChannelType *p = find(qname);
    if (!p) {
        const char *hint = (!qname || !strchr(qname,'.')) ? " (fully qualified type name expected)" : "";
        throw cRuntimeError("NED channel type \"%s\" not found%s", qname, hint);
    }
    return p;
}

cChannelType *cChannelType::getIdealChannelType()
{
    if (!idealChannelType) {
        idealChannelType = find("ned.IdealChannel");
        ASSERT(idealChannelType);
    }
    return idealChannelType;
}

cChannelType *cChannelType::getDelayChannelType()
{
    if (!delayChannelType) {
        delayChannelType = find("ned.DelayChannel");
        ASSERT(delayChannelType);
    }
    return delayChannelType;
}

cChannelType *cChannelType::getDatarateChannelType()
{
    if (!datarateChannelType) {
        datarateChannelType = find("ned.DatarateChannel");
        ASSERT(datarateChannelType);
    }
    return datarateChannelType;
}

cIdealChannel *cChannelType::createIdealChannel(const char *name)
{
    return cIdealChannel::create(name);
}

cDelayChannel *cChannelType::createDelayChannel(const char *name)
{
    return cDelayChannel::create(name);
}

cDatarateChannel *cChannelType::createDatarateChannel(const char *name)
{
    return cDatarateChannel::create(name);
}


