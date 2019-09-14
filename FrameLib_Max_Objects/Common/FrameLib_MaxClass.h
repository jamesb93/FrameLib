
#ifndef FRAMELIB_MAX_CLASS_H
#define FRAMELIB_MAX_CLASS_H

#include "MaxClass_Base.h"
#include "MaxBuffer.h"

#include "FrameLib_Global.h"
#include "FrameLib_Context.h"
#include "FrameLib_Parameters.h"
#include "FrameLib_DSP.h"
#include "FrameLib_Multistream.h"
#include "FrameLib_SerialiseGraph.h"

#include <string>
#include <deque>
#include <vector>
#include <unordered_map>

struct FrameLib_MaxProxy : public virtual FrameLib_Proxy
{
    t_object *mMaxObject;
};

struct FrameLib_MaxNRTAudio
{
    FrameLib_MaxNRTAudio(FrameLib_Multistream *object, t_symbol *buffer)
    : mObject(object), mBuffer(buffer) {}
    
    FrameLib_Multistream *mObject;
    t_symbol *mBuffer;
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////// Max Globals Class ///////////////////////////
//////////////////////////////////////////////////////////////////////////

class FrameLib_MaxGlobals : public MaxClass_Base
{
    struct Hash
    {
        size_t operator()(FrameLib_Context context) const
        {
            size_t hash = 0;
            hash ^= std::hash<void *>()(context.getGlobal()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            hash ^= std::hash<void *>()(context.getReference()) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
            
            return hash;
        }
    };
    
public:
    
    enum ConnectionMode : t_ptr_int { kConnect, kConfirm, kDoubleCheck };

    using MaxConnection = FrameLib_Connection<t_object, long>;
    
    // Error Notification Class
    
    struct ErrorNotifier : public FrameLib_ErrorReporter::HostNotifier
    {
        ErrorNotifier(FrameLib_Global **globalHandle)
        {
            mQelem = qelem_new(globalHandle, (method) &errorReport);
        }
        
        ~ErrorNotifier()
        {
            qelem_free(mQelem);
        }
        
        void notify()
        {
            qelem_set(mQelem);
        }
        
        static void errorReport(FrameLib_Global **globalHandle)
        {
            auto reports = (*globalHandle)->getErrors();

            for (auto it = reports->begin(); it != reports->end(); it++)
            {
                std::string errorText;
                t_object *object = it->getReporter() ? dynamic_cast<FrameLib_MaxProxy *>(it->getReporter())->mMaxObject : nullptr;
                t_object *userObject = object ? objectMethod<t_object *>(object, gensym("__fl.get_user_object")) : nullptr;
                
                it->getErrorText(errorText);
                
                if (userObject)
                {
                    if (it->getSource() == kErrorDSP)
                        object_error_obtrusive(userObject, errorText.c_str());
                    else
                        object_error(userObject, errorText.c_str());
                }
                else
                    ouchstring(errorText.c_str());
            }

            if (reports->isFull())
                error("*** FrameLib - too many errors - reporting only some ***");
        }
        
        t_qelem mQelem;
    };
    
    // Sync Check Class
    
    class SyncCheck
    {
        
    public:
        
        enum Mode { kDownOnly, kDown, kAcross };
        enum Action { kSyncComplete, kSync, kAttachAndSync };
        
        SyncCheck() : mGlobal(get()), mObject(nullptr), mTime(-1), mMode(kDownOnly) {}
        ~SyncCheck() { mGlobal->release(); }
        
        Action operator()(void *object, bool handlesAudio, bool isOutput)
        {
            const SyncCheck *info = mGlobal->getSyncCheck();
            
            if (info && (info->mTime != mTime || info->mObject != mObject))
            {
                set(info->mObject, info->mTime, info->mMode);
                return handlesAudio && object != mObject && (mMode != kAcross || isOutput) ? kAttachAndSync : kSync;
            }
            
            if (info && mMode == kAcross && info->mMode == kDown)
            {
                mMode = kDown;
                return handlesAudio && object != mObject && !isOutput ? kAttachAndSync : kSync;
            }
            
            return kSyncComplete;
        }
        
        void sync(void *object = nullptr, long time = -1, Mode mode = kDownOnly)
        {
            set(object, time, mode);
            mGlobal->setSyncCheck(object ? this : nullptr);
        }
        
        bool upwardsMode()  { return setMode(mGlobal->getSyncCheck(), kAcross); }
        void restoreMode()  { setMode(mGlobal->getSyncCheck(), mMode); }
        
    private:
        
        void set(void *object, long time, Mode mode)
        {
            mObject = object;
            mTime = time;
            mMode = mode;
        }
    
        bool setMode(SyncCheck *info, Mode mode)    { return info && info->mMode != kDownOnly && ((info->mMode = mode) == mode); }
        
        FrameLib_MaxGlobals *mGlobal;
        void *mObject;
        long mTime;
        Mode mMode;
    };

    // Convenience Pointer for automatic deletion and RAII
    
    struct ManagedPointer
    {
        ManagedPointer() : mPointer(get()) {}
        ~ManagedPointer() { mPointer->release(); }
        
        ManagedPointer(const ManagedPointer&) = delete;
        ManagedPointer& operator=(const ManagedPointer&) = delete;

        FrameLib_MaxGlobals *operator->() { return mPointer; }
        
    private:
        
        FrameLib_MaxGlobals *mPointer;
    };
    
    // Constructor and Destructor (public for max API, but use ManagedPointer from outside this class)
    
    FrameLib_MaxGlobals(t_symbol *sym, long ac, t_atom *av)
    : mReportContextErrors(false), mRTNotifier(&mRTGlobal), mNRTNotifier(&mNRTGlobal), mRTGlobal(nullptr), mNRTGlobal(nullptr), mSyncCheck(nullptr)
    {}

    // Getters and setters for max global items
    
    FrameLib_Global *getGlobal(bool nonRealtime) const  { return nonRealtime ? mNRTGlobal : mRTGlobal; }

    void clearQueue()                                   { mQueue.clear(); }
    void pushToQueue(t_object * object)                 { return mQueue.push_back(object); }
    
    t_object *popFromQueue()
    {
        if (mQueue.empty())
            return nullptr;
        
        t_object *object = mQueue.front();
        mQueue.pop_front();
        
        return object;
    }

    void addContextToResolve(FrameLib_Context context, t_object *object)
    {
        mContexts[context] = object;
        
        if (mContexts.size() == 1)
            defer_low(*this, (method) serviceContexts, nullptr, 0, nullptr);
    }
    
    void setReportContextErrors(bool report)            { mReportContextErrors = report; }
    bool getReportContextErrors() const                 { return mReportContextErrors; }

    void setConnection(MaxConnection connection, ConnectionMode mode)
    {
        mConnection = connection;
        mConnectionMode = mode;
    }

    void clearConnection()                              { setConnection(MaxConnection(), kConnect); }

    MaxConnection getConnection() const                 { return mConnection; }
    ConnectionMode getConnectionMode() const            { return mConnectionMode; }
    
    SyncCheck *getSyncCheck() const                     { return mSyncCheck; }
    void setSyncCheck(SyncCheck *check)                 { mSyncCheck = check; }
    
private:
    
    static void serviceContexts(FrameLib_MaxGlobals *x)
    {
        for (auto it = x->mContexts.begin(); it != x->mContexts.end(); it++)
        {
            FrameLib_Context context = it->first;
            objectMethod(it->second, gensym("__fl.resolve_graph"), &context);
        }
            
        x->mContexts.clear();
    }
    
    // Generate some relevant thread priorities
    
    static FrameLib_Thread::Priorities priorities(bool nonRealtime)
    {
        if (nonRealtime)
            return { 31, 31, 31, SCHED_OTHER, true };
#ifdef __APPLE__
        if (maxversion() >= 0x800)
            return { 31, 31, 43, SCHED_RR, false };
#endif
        return FrameLib_Thread::defaultPriorities();
    }
    
    // Get and release the max global items (singleton)
    
    static FrameLib_MaxGlobals *get()
    {
        const char maxGlobalClass[] = "__fl.max_global_items";
        t_symbol *nameSpace = gensym("__fl.framelib_private");
        t_symbol *globalTag = gensym("__fl.max_global_tag");
     
        // Make sure the max globals class exists

        if (!class_findbyname(CLASS_NOBOX, gensym(maxGlobalClass)))
            makeClass<FrameLib_MaxGlobals>(CLASS_NOBOX, maxGlobalClass);
        
        // See if an object is registered (otherwise make object and register it...)
        
        FrameLib_MaxGlobals *x = (FrameLib_MaxGlobals *) object_findregistered(nameSpace, globalTag);
        
        if (!x)
            x = (FrameLib_MaxGlobals *) object_register(nameSpace, globalTag, object_new_typed(CLASS_NOBOX, gensym(maxGlobalClass), 0, nullptr));
        
        FrameLib_Global::get(&x->mRTGlobal, priorities(false), &x->mRTNotifier);
        FrameLib_Global::get(&x->mNRTGlobal, priorities(true), &x->mNRTNotifier);
        
        return x;
    }
    
    void release()
    {
        FrameLib_Global::release(&mRTGlobal);
        FrameLib_Global::release(&mNRTGlobal);

        if (!mRTGlobal)
        {
            assert(!mNRTGlobal && "Reference counting error");
            
            object_unregister(this);
            object_free(this);
        }
    }
    
    // Connection Info
    
    MaxConnection mConnection;
    ConnectionMode mConnectionMode;
    bool mReportContextErrors;
    
    // Member Objects / Pointers
    
    ErrorNotifier mRTNotifier;
    ErrorNotifier mNRTNotifier;
    
    std::deque<t_object *> mQueue;
    std::unordered_map<FrameLib_Context, t_object *, Hash> mContexts;
    
    FrameLib_Global *mRTGlobal;
    FrameLib_Global *mNRTGlobal;
    
    SyncCheck *mSyncCheck;
};

//////////////////////////////////////////////////////////////////////////
////////////////////// Mutator for Synchronisation ///////////////////////
//////////////////////////////////////////////////////////////////////////

class Mutator : public MaxClass_Base
{
    using SyncCheck = FrameLib_MaxGlobals::SyncCheck;
    using FLObject = FrameLib_Multistream;
    
public:
    
    Mutator(t_symbol *sym, long ac, t_atom *av)
    {
        mObject = reinterpret_cast<t_object *>(ac ? atom_getobj(av) : nullptr);
        FLObject *object = mObject ? objectMethod<FLObject *>(mObject, gensym("__fl.get_framelib_object")) : nullptr;
        mMode = object && object->getType() == kOutput ? SyncCheck::kDownOnly : SyncCheck::kDown;
    }
    
    static void classInit(t_class *c, t_symbol *nameSpace, const char *classname)
    {
        addMethod<Mutator, &Mutator::mutate>(c, "signal");
    }
    
    void mutate(t_symbol *sym, long ac, t_atom *av)
    {
        mSyncChecker.sync(mObject, gettime(), mMode);
        objectMethod(mObject, gensym("sync"));
        mSyncChecker.sync();
    }
    
private:
    
    SyncCheck mSyncChecker;
    SyncCheck::Mode mMode;
    t_object *mObject;
};

//////////////////////////////////////////////////////////////////////////
////////////////////// Wrapper for Synchronisation ///////////////////////
//////////////////////////////////////////////////////////////////////////

template <class T>
class Wrapper : public MaxClass_Base
{
    
public:
    
    // Initialise Class
    
    static method *sigMethodCache()
    {
        static method sigMethod;
        
        return &sigMethod;
    }
    
    static void classInit(t_class *c, t_symbol *nameSpace, const char *classname)
    {
        addMethod<Wrapper<T>, &Wrapper<T>::parentpatcher>(c, "parentpatcher");
        addMethod<Wrapper<T>, &Wrapper<T>::subpatcher>(c, "subpatcher");
        addMethod<Wrapper<T>, &Wrapper<T>::assist>(c, "assist");
        addMethod<Wrapper<T>, &Wrapper<T>::anything>(c, "anything");
        addMethod<Wrapper<T>, &Wrapper<T>::sync>(c, "sync");
        addMethod<Wrapper<T>, &Wrapper<T>::dsp>(c);

        addMethod(c, (method) &patchLineUpdate, "patchlineupdate");
        addMethod(c, (method) &connectionAccept, "connectionaccept");
        addMethod(c, (method) &unwrap, "__fl.wrapper_unwrap");
        addMethod(c, (method) &isWrapper, "__fl.wrapper_is_wrapper");

        addMethod(c, (method) &Wrapper<T>::dblclick, "dblclick");

        // N.B. MUST add signal handling after dspInit to override the builtin responses
        
        dspInit(c);
        *sigMethodCache() = class_method(c, gensym("signal"));
        addMethod<Wrapper<T>, &Wrapper<T>::anything>(c, "signal");
        
        // Make sure the mutator class exists
        
        const char mutatorClassName[] = "__fl.signal.mutator";
        
        if (!class_findbyname(CLASS_NOBOX, gensym(mutatorClassName)))
            Mutator::makeClass<Mutator>(CLASS_NOBOX, mutatorClassName);
        
        // Declare attribute in wrapper as well as FrameLib_MaxClass
        
        CLASS_ATTR_SYM(c, "buffer", ATTR_FLAGS_NONE, Wrapper<T>, mObject);
        CLASS_ATTR_ACCESSORS(c, "buffer", &Wrapper<T>::bufferGet, &Wrapper<T>::bufferSet);
    }

    // Constructor and Destructor
    
    Wrapper(t_symbol *s, long argc, t_atom *argv) : mParentPatch(gensym("#P")->s_thing)
    {
        // Create patcher (you must report this as a subpatcher to get audio working)
        
        t_dictionary *d = dictionary_new();
        t_atom a;
        t_atom *av = nullptr;
        long ac = 0;
        
        atom_setparse(&ac, &av, "@defrect 0 0 300 300");
        attr_args_dictionary(d, static_cast<short>(ac), av);
        atom_setobj(&a, d);
        mPatch = toUnique(object_new_typed(CLASS_NOBOX, gensym("jpatcher"),1, &a));
        
        // Get box text (and strip object name - replace with stored name in case the name is an alias)
        
        t_object *textfield = nullptr;
        const char *text = nullptr;
        std::string newObjectText = accessClassName<Wrapper>()->c_str();

        object_obex_lookup(this, gensym("#B"), &textfield);
        
        if ((textfield = jbox_get_textfield(textfield)))
        {
            text = objectMethod<char *>(textfield, gensym("getptr"));
            text = strchr(text, ' ');
            
            if (text)
                newObjectText += text;
        }
        
        // Set the patch association
        
        objectMethod(mPatch.get(), gensym("setassoc"), this);
        
        // Make internal object and disallow editing

        mObject = jbox_get_object((t_object *) newobject_sprintf(mPatch.get(), "@maxclass newobj @text \"unsynced.%s\" @patching_rect 0 0 30 10", newObjectText.c_str()));
        objectMethod(mPatch.get(), gensym("noedit"));
        
        // Free the dictionary
        
        object_free(d);
        
        // For realtime versions setup DSP and make/connect mutator
        
        if (isRealtime())
        {
            dspSetup(1);
            
            atom_setobj(&a, mObject);
            mMutator = toUnique(object_new_typed(CLASS_NOBOX, gensym("__fl.signal.mutator"), 1, &a));
            
            outlet_add(outlet_nth(mObject, 0), inlet_nth(mMutator.get(), 0));
        }
        else
            mMutator = nullptr;
        
        // Get the object itself (typed)
        
        T *internal = object();
        
        long numIns = internal->getNumIns() + (internal->supportsOrderingConnections() ? 1 : 0);
        long numOuts = internal->getNumOuts();
        long numAudioIns = internal->getNumAudioIns();
        long numAudioOuts = internal->getNumAudioOuts();
        long numLocalAudioIns = std::max(0L, numAudioIns - 1);
        long numLocalAudioOuts = std::max(0L, numAudioOuts - 1);
        
        // Create I/O
        
        mInOutlets.resize(numIns + numLocalAudioIns);
        mProxyIns.resize(numIns + numLocalAudioIns);
        mOuts.resize(numOuts);
        
        // Inlets for messages/signals (we need one audio in for the purposes of sync)

        for (long i = numIns + numLocalAudioIns - 1; i >= 0 ; i--)
        {
            mInOutlets[i] = toUnique(outlet_new(nullptr, nullptr));
            mProxyIns[i] = toUnique(i ? proxy_new(this, i, &mProxyNum) : nullptr);
        }
        
        // Outlets for messages/signals
        
        for (long i = numOuts - 1; i >= 0 ; i--)
            mOuts[i] = outlet_new(this, nullptr);
        for (long i = numLocalAudioOuts - 1; i >= 0 ; i--)
            outlet_new(this, "signal");
        
        // Connect inlets (all types)
        
        for (long i = 0; i < numLocalAudioIns + numIns; i++)
        {
            // Get the inlet (if there is none then add the object directly as it has only one inlet)
            
            void *p = inlet_nth(mObject, offset(i));
            outlet_add(mInOutlets[i].get(), p ? p : mObject);
        }
        
        // Connect non-audio outlets
        
        for (long i = 0; i < numOuts; i++)
            outlet_add(outlet_nth(mObject, i + numAudioOuts), mOuts[i]);
    }
    
    // Standard methods
    
    void parentpatcher(t_object **parent)
    {
        *parent = mParentPatch;
    }
    
    void *subpatcher(long index, void *arg)
    {
        return (((t_ptr_uint) arg <= 1) || ((t_ptr_uint) arg > 1 && !NOGOOD(arg))) && index == 0 ? mPatch.get() : nullptr;
    }
    
    void assist(void *b, long m, long a, char *s)
    {
        object()->assist(b, m, offset(a), s);
    }
    
    void sync()
    {
        // Set the order of the wrapper after the owned object by doing this before calling internal sync
        
        if (isRealtime())
            (*sigMethodCache())(this);
        
        object()->sync();
    }
    
    void dsp(t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
    {
        if (isRealtime() && object()->getType() == kOutput)
            addPerform<Wrapper, &Wrapper<T>::perform>(dsp64);
    }
    
    void perform(t_object *dsp64, double **ins, long numins, double **outs, long numouts, long vec_size, long flags, void *userparam)
    {
        std::vector<double*> &internalOuts = object()->getAudioOuts();
        
        // Copy to output
        
        for (size_t i = 0; i < internalOuts.size(); i++)
            std::copy(internalOuts[i], internalOuts[i] + vec_size, outs[i]);
    }
    
    void anything(t_symbol *sym, long ac, t_atom *av)
    {
        outlet_anything(mInOutlets[getInlet()].get(), sym, static_cast<int>(ac), av);
    }
    
    // Double-click for buffer viewing
    
    static void dblclick(Wrapper *x)
    {
        T::dblclick(x->object());
    }
    
    // Buffer attribute
    
    static t_max_err bufferGet(Wrapper *x, t_object *attr, long *argc, t_atom **argv)
    {
        char alloc;
        atom_alloc(argc, argv, &alloc);
        atom_setsym(*argv, x->object()->mBuffer);
        
        return MAX_ERR_NONE;
    }
    
    static t_max_err bufferSet(Wrapper *x, t_object *attr, long argc, t_atom *argv)
    {
        x->object()->mBuffer = argv ? atom_getsym(argv) : gensym("");
        
        return MAX_ERR_NONE;
    }
    
    // External methods (A_CANT)
    
    static t_max_err patchLineUpdate(Wrapper *x, t_object *line, long type, t_object *src, long srcout, t_object *dst, long dstin)
    {
        if (*x == dst)
            return T::extPatchLineUpdate(x->object(), line, type, src, srcout, x->mObject, x->offset(dstin));
        else
            return T::extPatchLineUpdate(x->object(), line, type, x->mObject, x->offset(srcout), dst, dstin);
    }
    
    static t_ptr_int connectionAccept(Wrapper *x, t_object *dst, long srcout, long dstin, t_object *op, t_object *ip)
    {
        return T::extConnectionAccept(x->object(), dst, x->offset(srcout), dstin, op, ip);
    }
    
    static void *unwrap(Wrapper *x, long* idx)
    {
        *idx = x->offset(*idx);
        return x->mObject;
    }
    
    static void *isWrapper(Wrapper *x)
    {
        return (void *) 1;
    }
    
private:
    
    T *object() { return (T *) mObject; }

    bool isRealtime() { return object()->isRealtime(); }
    
    long offset(long connectionIdx) { return isRealtime() ? connectionIdx + 1 : connectionIdx; }

    // Owned Objects (need freeing)
    
    unique_object_ptr mPatch;
    unique_object_ptr mMutator;
    
    // Unowned objects (the internal object is owned by the patch)
    
    t_object *mParentPatch;
    t_object *mObject;
    
    // Inlets (must be freed)
    
    std::vector<unique_object_ptr> mInOutlets;
    std::vector<unique_object_ptr> mProxyIns;
    
    // Outlets (don't need to free)
    
    std::vector<void *> mOuts;
    
    // Dummy for stuffloc on proxies
    
    long mProxyNum;
};

//////////////////////////////////////////////////////////////////////////
/////////////////////// FrameLib Max Object Class ////////////////////////
//////////////////////////////////////////////////////////////////////////

enum MaxObjectArgsMode { kAsParams, kAllInputs, kDistribute };

template <class T, MaxObjectArgsMode argsMode = kAsParams>
class FrameLib_MaxClass : public MaxClass_Base
{
    using ConnectionMode = FrameLib_MaxGlobals::ConnectionMode;
    using FLObject = FrameLib_Multistream;
    using FLConnection = FrameLib_Object<FLObject>::Connection;
    using MaxConnection = FrameLib_MaxGlobals::MaxConnection;

    struct ConnectionConfirmation
    {
        ConnectionConfirmation(MaxConnection connection, long inIdx)
        : mConfirm(false), mConnection(connection), mInIndex(inIdx) {}
        
        bool confirm(MaxConnection connection, long inIdx)
        {
            bool result = inIdx == mInIndex && connection == mConnection;
            
            if (result)
                mConfirm = true;
            
            return result;
        }
        
        bool mConfirm;
        MaxConnection mConnection;
        long mInIndex;
    };
    
public:
    
    // Class Initialisation (must explicitly give U for classes that inherit from FrameLib_MaxClass<>)
    
    template <class U = FrameLib_MaxClass<T, argsMode>>
    static void makeClass(const char *className)
    {
        // If handles audio/scheduler then make wrapper class and name the inner object differently..
        
        std::string internalClassName = className;
        
        if (T::handlesAudio())
        {
            Wrapper<U>:: template makeClass<Wrapper<U>>(CLASS_BOX, className);
            internalClassName.insert(0, "unsynced.");
        }
        
        MaxClass_Base::makeClass<U>(CLASS_BOX, internalClassName.c_str());
    }
    
    static void classInit(t_class *c, t_symbol *nameSpace, const char *classname)
    {
        addMethod<FrameLib_MaxClass<T>, &FrameLib_MaxClass<T>::assist>(c, "assist");
        addMethod<FrameLib_MaxClass<T>, &FrameLib_MaxClass<T>::info>(c, "info");
        addMethod<FrameLib_MaxClass<T>, &FrameLib_MaxClass<T>::frame>(c, "frame");
        addMethod<FrameLib_MaxClass<T>, &FrameLib_MaxClass<T>::sync>(c, "sync");
        addMethod<FrameLib_MaxClass<T>, &FrameLib_MaxClass<T>::dsp>(c);
        
        if (T::handlesAudio())
        {
            addMethod<FrameLib_MaxClass<T>, &FrameLib_MaxClass<T>::reset>(c, "reset");
            addMethod<FrameLib_MaxClass<T>, &FrameLib_MaxClass<T>::process>(c, "process");
            
            addMethod(c, (method) &extFindAudio, "__fl.find_audio_objects");
        }
        
        addMethod(c, (method) &extPatchLineUpdate, "patchlineupdate");
        addMethod(c, (method) &extConnectionAccept, "connectionaccept");
        addMethod(c, (method) &extResolveGraph, "__fl.resolve_graph");
        addMethod(c, (method) &extResolveConnections, "__fl.resolve_connections");
        addMethod(c, (method) &extMarkUnresolved, "__fl.mark_unresolved");
        addMethod(c, (method) &extAutoOrderingConnections, "__fl.auto_ordering_connections");
        addMethod(c, (method) &extClearAutoOrderingConnections, "__fl.clear_auto_ordering_connections");
        addMethod(c, (method) &extGetDSPObject, "__fl.get_dsp_object");
        addMethod(c, (method) &extSetDSPObject, "__fl.set_dsp_object");
        addMethod(c, (method) &extReset, "__fl.reset");
        addMethod(c, (method) &extIsConnected, "__fl.is_connected");
        addMethod(c, (method) &extConnectionConfirm, "__fl.connection_confirm");
        addMethod(c, (method) &extConnectionUpdate, "__fl.connection_update");
        addMethod(c, (method) &extGetFLObject, "__fl.get_framelib_object");
        addMethod(c, (method) &extGetUserObject, "__fl.get_user_object");
        
        class_addmethod(c, (method) &codeexport, "export", A_SYM, A_SYM, 0);
        
        if (T::handlesAudio())
        {
            dspInit(c);

            addMethod(c, (method) &FrameLib_MaxClass<T>::dblclick, "dblclick");
            CLASS_ATTR_SYM(c, "buffer", ATTR_FLAGS_NONE, FrameLib_MaxClass<T>, mBuffer);
        }
    }

    // Check if a patch in memory matches a symbol representing a path
    
    static bool comparePatchWithName(t_object *patch, t_symbol *name)
    {
        char fileName[MAX_FILENAME_CHARS];
        t_fourcc validTypes[TYPELIST_SIZE];
        short outvol = 0, numTypes = 0;
        t_fourcc outtype = 0;
        
        typelist_make(validTypes, TYPELIST_MAXFILES, &numTypes);
        strncpy_zero(fileName, name->s_name, MAX_FILENAME_CHARS);
        locatefile_extended(fileName, &outvol, &outtype, validTypes, numTypes);
        
        return !strcmp(jpatcher_get_filename(patch)->s_name, fileName);
    }
    
    // Find the patcher for the context

    static t_object *contextPatcher(t_object *patch)
    {
        bool traverse = true;
        
        for (t_object *parent = nullptr; traverse && (parent = jpatcher_get_parentpatcher(patch)); patch = traverse ? parent : patch)
        {
            t_object *assoc = getAssociation(patch);
            
            // Traverse if the patch is in a box (subpatcher or abstraction) it belongs to a wrapper
            
            traverse = jpatcher_get_box(patch) || (assoc && objectMethod(assoc, gensym("__fl.wrapper_is_wrapper")));
            
            if (!traverse && !assoc)
            {
                // Get text of loading object in parent if there is no association (patch is currently loading)

                char *text = nullptr;
                
                for (t_object *b = jpatcher_get_firstobject(parent); b && !text; b = jbox_get_nextobject(b))
                    if (jbox_get_maxclass(b) == gensym("newobj") && jbox_get_textfield(b))
                        text = objectMethod<char *>(jbox_get_textfield(b), gensym("getptr"));

                if (text)
                {
                    // Get the first item in the box as a symbol
                
                    t_atombuf *atomBuffer = (t_atombuf *) atombuf_new(0, nullptr);
                    atombuf_text(&atomBuffer, &text, static_cast<long>(strlen(text)));
                    t_symbol *objectName = atom_getsym(atomBuffer->a_argv);
                    atombuf_free(atomBuffer);
                    
                    // Check if the patch is loading in a subpatcher otherwise check if it is loading in an abstraction
                    
                    traverse = objectName == gensym("p") || objectName == gensym("patcher") || comparePatchWithName(patch, objectName);
                }
                else
                {
                    // FIX - this is not perfect for bpatchers, but it is relatively safe for now
                    
                    for (t_object *b = jpatcher_get_firstobject(parent); b && !traverse; b = jbox_get_nextobject(b))
                        if (jbox_get_maxclass(b) == gensym("bpatcher"))
                            traverse = comparePatchWithName(patch, object_attr_getsym(b, gensym("name")));
                }
            }
        }
        
        return patch;
    }
    
    // Detect the user object at load time
    
    t_object *detectUserObjectAtLoad()
    {
        t_object *patch = gensym("#P")->s_thing;
        t_object *assoc = getAssociation(patch);
    
        return (assoc && objectMethod(assoc, gensym("__fl.wrapper_is_wrapper"))) ? assoc : *this;
    }
    
    // Detect non-realtime setting
    
    static bool detectRealtime(long argc, t_atom * argv)
    {
        return (!argc || atom_getsym(argv) != gensym("nrt"));
    }
    
    // Constructor and Destructor
    
    FrameLib_MaxClass(t_symbol *s, long argc, t_atom *argv, FrameLib_MaxProxy *proxy = new FrameLib_MaxProxy())
    : mFrameLibProxy(proxy)
    , mConfirmation(nullptr)
    , mContextPatch(contextPatcher(gensym("#P")->s_thing))
    , mUserObject(detectUserObjectAtLoad())
    , mDSPObject(nullptr)
    , mSpecifiedStreams(1)
    , mRealtime(handlesAudio() ? detectRealtime(argc, argv) : false)
    , mConnectionsUpdated(false)
    , mResolved(false)
    , mBuffer(gensym(""))
    {
        // Ignore non-realtime specifier
        
        if (handlesAudio() && !detectRealtime(argc, argv))
        {
            argc--;
            argv++;
        }
        
        // Deal with attributes for non-realtime objects (and to correctly report issues otherwise
        
        attr_args_process(this, argc, argv);
        argc = attr_args_offset(argc, argv);
        
        // Stream count
        
        if (argc && getStreamCount(argv))
        {
            mSpecifiedStreams = getStreamCount(argv);
            argv++;
            argc--;
        }
        
        // Object creation with parameters and arguments (N.B. the object is not a member due to size restrictions)

        FrameLib_Parameters::AutoSerial serialisedParameters;
        parseParameters(serialisedParameters, argc, argv);
        FrameLib_Context context(mGlobal->getGlobal(!isRealtime()), mContextPatch);
        mFrameLibProxy->mMaxObject = *this;
        mObject.reset(new T(context, &serialisedParameters, mFrameLibProxy.get(), mSpecifiedStreams));
        parseInputs(argc, argv);
        
        long numIns = getNumIns() + (supportsOrderingConnections() ? 1 : 0);

        mInputs.resize(numIns);
        mOutputs.resize(getNumOuts());
        
        // Create frame inlets and outlets
        
        // N.B. - we create a proxy if the inlet is not the first inlet (not the first frame input or the object handles realtime audio)
        
        for (long i = numIns - 1; i >= 0; i--)
            mInputs[i] = toUnique((i || handlesRealtimeAudio()) ? proxy_new(this, getNumAudioIns() + i, &mProxyNum) : nullptr);
        
        for (long i = getNumOuts(); i > 0; i--)
            mOutputs[i - 1] = outlet_new(this, nullptr);
        
        // Setup for audio, even if the object doesn't handle it, so that dsp recompile works correctly
        
        if (handlesRealtimeAudio())
        {
            dspSetup(getNumAudioIns());
            
            for (long i = 0; i < getNumAudioOuts(); i++)
                outlet_new(this, "signal");
        
            // Add a sync outlet if we need to handle audio
        
            mSyncIn = toUnique(outlet_new(nullptr, nullptr));
            outlet_add(mSyncIn.get(), inlet_nth(*this, 0));
        }
    }

    ~FrameLib_MaxClass()
    {
        if (isRealtime())
        {
            if (handlesAudio())
            {
                dspFree();
                traversePatch(gensym("__fl.set_dsp_object"), static_cast<void *>(nullptr));
            }
            else
                dspSetBroken(mDSPObject);
        }
    }
    
    void assist(void *b, long m, long a, char *s)
    {
        if (m == ASSIST_OUTLET)
        {
            if (a == 0 && handlesRealtimeAudio())
                sprintf(s,"(signal) Audio Synchronisation Output" );
            else if (a < getNumAudioOuts())
                sprintf(s,"(signal) %s", mObject->audioInfo(a - 1).c_str());
            else
                sprintf(s,"(frame) %s", mObject->outputInfo(a - getNumAudioOuts()).c_str());
        }
        else
        {
            if (a == 0 && handlesRealtimeAudio())
                sprintf(s,"(signal) Audio Synchronisation Input");
            else if (a < getNumAudioIns())
                sprintf(s,"(signal) %s", mObject->audioInfo(a - 1).c_str());
            else
            {
                if (supportsOrderingConnections() && a == getNumAudioIns() + getNumIns())
                    sprintf(s,"(frame) Ordering Input");
                else
                    sprintf(s,"(frame) %s", mObject->inputInfo(a - getNumAudioIns()).c_str());
            }
        }
    }
    
    static void codeexport(FrameLib_MaxClass *x, t_symbol *className, t_symbol *path)
    {
        char conformedPath[MAX_PATH_CHARS];
                
        if (!x->mDSPObject || !sys_getdspobjdspstate(x->mDSPObject))
        {
            x->resolveGraph();
            x->traversePatch(gensym("__fl.mark_unresolved"));
        }

        path_nameconform(path->s_name, conformedPath, PATH_STYLE_NATIVE, PATH_TYPE_BOOT);
        ExportError error = exportGraph(x->mObject.get(), conformedPath, className->s_name);
        
        if (error == kExportPathError)
            object_error(x->mUserObject, "couldn't write to or find specified path");
        else if (error == kExportWriteError)
            object_error(x->mUserObject, "couldn't write file");
    }
    
    void info(t_symbol *sym, long ac, t_atom *av)
    {
        // Determine what to post
        
        enum InfoFlags { kInfoDesciption = 0x01, kInfoInputs = 0x02, kInfoOutputs = 0x04, kInfoParameters = 0x08 };
        bool verbose = true;
        long flags = 0;
        
        while (ac--)
        {
            t_symbol *type = atom_getsym(av++);
            
            if (type == gensym("description"))          flags |= kInfoDesciption;
            else if (type == gensym("inputs"))          flags |= kInfoInputs;
            else if (type == gensym("outputs"))         flags |= kInfoOutputs;
            else if (type == gensym("io"))              flags |= kInfoInputs | kInfoOutputs;
            else if (type == gensym("parameters"))      flags |= kInfoParameters;
            else if (type == gensym("quick"))           verbose = false;
        }
        
        flags = !flags ? (kInfoDesciption | kInfoInputs | kInfoOutputs | kInfoParameters) : flags;
        
        // Start Tag
        
        object_post(mUserObject, "********* %s *********", object_classname(mUserObject)->s_name);

        // Description
        
        if (flags & kInfoDesciption)
        {
            object_post(mUserObject, "--- Description ---");
            postSplit(mObject->objectInfo(verbose).c_str(), "", "-");
        }
        
        // IO
        
        if (flags & kInfoInputs)
        {
            object_post(mUserObject, "--- Input List ---");
            if (argsMode == kAllInputs)
                object_post(mUserObject, "N.B. - arguments set the fixed array values for all inputs.");
            if (argsMode == kDistribute)
                object_post(mUserObject, "N.B - arguments are distributed one per input.");
            for (long i = 0; i < (long) mObject->getNumAudioIns(); i++)
                object_post(mUserObject, "Audio Input %ld: %s", i + 1, mObject->audioInfo(i, verbose).c_str());
            for (long i = 0; i < getNumIns(); i++)
                object_post(mUserObject, "Frame Input %ld [%s]: %s", i + 1, frameTypeString(mObject->inputType(i)), mObject->inputInfo(i, verbose).c_str());
            if (supportsOrderingConnections())
                object_post(mUserObject, "Ordering Input [%s]: Connect to ensure ordering", frameTypeString(kFrameAny));
        }
        
        if (flags & kInfoOutputs)
        {
            object_post(mUserObject, "--- Output List ---");
            for (long i = 0; i < (long) mObject->getNumAudioOuts(); i++)
                object_post(mUserObject, "Audio Output %ld: %s", i + 1, mObject->audioInfo(i, verbose).c_str());
            for (long i = 0; i < getNumOuts(); i++)
                object_post(mUserObject, "Frame Output %ld [%s]: %s", i + 1, frameTypeString(mObject->outputType(i)), mObject->outputInfo(i, verbose).c_str());
        }
        
        // Parameters
        
        if (flags & kInfoParameters)
        {
            object_post(mUserObject, "--- Parameter List ---");
            
            const FrameLib_Parameters *params = mObject->getParameters();
            if (!params || !params->size()) object_post(mUserObject, "< No Parameters >");
            
            // Loop over parameters
            
            for (unsigned long i = 0; params && i < params->size(); i++)
            {
                FrameLib_Parameters::Type type = params->getType(i);
                FrameLib_Parameters::NumericType numericType = params->getNumericType(i);
                std::string defaultStr = params->getDefaultString(i);

                // Name, type and default value
                
                if (defaultStr.size())
                    object_post(mUserObject, "Parameter %lu: %s [%s] (default: %s)", i + 1, params->getName(i).c_str(), params->getTypeString(i).c_str(), defaultStr.c_str());
                else
                    object_post(mUserObject, "Parameter %lu: %s [%s]", i + 1, params->getName(i).c_str(), params->getTypeString(i).c_str());

                // Verbose - arguments, range (for numeric types), enum items (for enums), array sizes (for arrays), description
                
                if (verbose)
                {
                    if (argsMode == kAsParams && params->getArgumentIdx(i) >= 0)
                        object_post(mUserObject, "- Argument: %ld", params->getArgumentIdx(i) + 1);
                    if (numericType == FrameLib_Parameters::kNumericInteger || numericType == FrameLib_Parameters::kNumericDouble)
                    {
                        switch (params->getClipMode(i))
                        {
                            case FrameLib_Parameters::kNone:    break;
                            case FrameLib_Parameters::kMin:     object_post(mUserObject, "- Min Value: %lg", params->getMin(i));                        break;
                            case FrameLib_Parameters::kMax:     object_post(mUserObject, "- Max Value: %lg", params->getMax(i));                        break;
                            case FrameLib_Parameters::kClip:    object_post(mUserObject, "- Clipped: %lg-%lg", params->getMin(i), params->getMax(i));   break;
                        }
                    }
                    if (type == FrameLib_Parameters::kEnum)
                        for (unsigned long j = 0; j <= static_cast<unsigned long>(params->getMax(i)); j++)
                            object_post(mUserObject, "   [%ld] - %s", j, params->getItemString(i, j).c_str());
                    else if (type == FrameLib_Parameters::kArray)
                        object_post(mUserObject, "- Array Size: %ld", params->getArraySize(i));
                    else if (type == FrameLib_Parameters::kVariableArray)
                        object_post(mUserObject, "- Array Max Size: %ld", params->getArrayMaxSize(i));
                    postSplit(params->getInfo(i).c_str(), "- ", "-");
                }
            }
        }
    }

    // IO and Mode Helpers
    
    ObjectType getType() const                  { return mObject->getType(); }
    
    bool isRealtime() const                     { return mRealtime; }
    bool handlesAudio() const                   { return T::handlesAudio(); }
    bool handlesRealtimeAudio() const           { return handlesAudio() && isRealtime(); }
    bool supportsOrderingConnections() const    { return mObject->supportsOrderingConnections(); }

    long audioIOSize(long chans) const          { return isRealtime() ? (chans + (handlesAudio() ? 1 : 0)) : 0; }

    long getNumIns() const                      { return (long) mObject->getNumIns(); }
    long getNumOuts() const                     { return (long) mObject->getNumOuts(); }
    long getNumAudioIns() const                 { return audioIOSize(mObject->getNumAudioIns()); }
    long getNumAudioOuts() const                { return audioIOSize(mObject->getNumAudioOuts()); }
    
    unsigned long getSpecifiedStreams() const   { return mSpecifiedStreams; }

    // Perform and DSP

    void perform(t_object *dsp64, double **ins, long numins, double **outs, long numouts, long vec_size, long flags, void *userparam)
    {
        if (static_cast<long>(mSigOuts.size()) != (numouts - 1))
        {
            for (long i = 1; i < numouts; i++)
                mSigOuts.push_back(outs[i]);
        }
            
        // N.B. Plus one due to sync inputs
        
        mObject->blockUpdate(ins + 1, outs + 1, vec_size);
    }

    void dsp(t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags)
    {
        if (!isRealtime())
            return;
        
        mSigOuts.clear();
        
        // Resolve connections (in case there are no schedulers left in the patch) and mark unresolved for next time
        
        resolveConnections();
        mResolved = false;
        
        // Reset DSP
        
        mObject->reset(samplerate, maxvectorsize);
        
        // Add a perform routine to the chain if the object handles audio
        
        if (handlesAudio())
            addPerform<FrameLib_MaxClass, &FrameLib_MaxClass<T>::perform>(dsp64);
    }

    // Non-realtime processing
    
    static unsigned long maxBlockSize() { return 16384UL; }
    
    void reset(def_double sampleRate = 0.0)
    {
        checkGraph(sampleRate > 0.0 ? sampleRate.mValue : sys_getsr(), true);
    }
    
    void process(t_atom_long length)
    {
        unsigned long updateLength = length > 0 ? length : 0;
        unsigned long time = static_cast<unsigned long>(mObject->getBlockTime());
        
        if (!updateLength)
            return;
        
        checkGraph(0.0, false);
        
        // Retrieve all the audio objects in a list
        
        std::vector<FrameLib_MaxNRTAudio> audioObjects;
        traversePatch(gensym("__fl.find_audio_objects"), &audioObjects);
        
        // Set up buffers
        
        unsigned long maxAudioIO = 0;
        
        for (auto it = audioObjects.begin(); it != audioObjects.end(); it++)
            maxAudioIO = std::max(maxAudioIO, it->mObject->getNumAudioChans());
        
        std::vector<double> audioBuffer(maxBlockSize() * maxAudioIO);
        std::vector<double *> ioBuffers(maxAudioIO);
        
        for (unsigned long i = 0; i < maxAudioIO; i++)
            ioBuffers[i] = audioBuffer.data() + i * maxBlockSize();
        
        // Loop to process audio
        
        for (unsigned long i = 0; i < updateLength; i += maxBlockSize())
        {
            unsigned long blockSize = std::min(maxBlockSize(), updateLength - i);
            
            // Process inputs and schedulers (block controls object lifetime)
            
            if (true)
            {
                FrameLib_AudioQueue queue;
                
                for (auto it = audioObjects.begin(); it != audioObjects.end(); it++)
                {
                    if (it->mObject->getType() == kOutput)
                        continue;
                    
                    read(it->mBuffer, ioBuffers.data(), it->mObject->getNumAudioIns(), blockSize, time + i);
                    it->mObject->blockUpdate(ioBuffers.data(), nullptr, blockSize, queue);
                }
            }
            
            // Process outputs
            
            for (auto it = audioObjects.begin(); it != audioObjects.end(); it++)
            {
                if (it->mObject->getType() != kOutput)
                    continue;
                
                it->mObject->blockUpdate(nullptr, ioBuffers.data(), blockSize);
                write(it->mBuffer, ioBuffers.data(), it->mObject->getNumAudioOuts(), blockSize, time + i);
            }
        }
    }
    
    // Audio Synchronisation
    
    void sync()
    {
        FrameLib_MaxGlobals::SyncCheck::Action action = mSyncChecker(this, handlesAudio(), getType() == kOutput);
       
        if (action != FrameLib_MaxGlobals::SyncCheck::kSyncComplete && handlesAudio() && !mResolved)
        {
            resolveGraph();
            traversePatch(gensym("__fl.set_dsp_object"), this);
        }
        
        if (action == FrameLib_MaxGlobals::SyncCheck::kAttachAndSync)
            outlet_anything(mSyncIn.get(), gensym("signal"), 0, nullptr);
        
        if (action != FrameLib_MaxGlobals::SyncCheck::kSyncComplete)
        {
            for (long i = getNumOuts(); i > 0; i--)
                outlet_anything(mOutputs[i - 1], gensym("sync"), 0, nullptr);
            
            if (mSyncChecker.upwardsMode())
            {
                for (long i = 0; i < getNumIns(); i++)
                    if (isConnected(i))
                        objectMethod(getConnection(i).mObject, gensym("sync"));
                
                if (supportsOrderingConnections())
                    for (long i = 0; i < getNumOrderingConnections(); i++)
                        objectMethod(getOrderingConnection(i).mObject, gensym("sync"));
                
                mSyncChecker.restoreMode();
            }
        }
    }
    
    // Double-click for buffer viewing
    
    static void dblclick(FrameLib_MaxClass *x)
    {
        if (x->isRealtime())
            return;
        
        MaxBufferAccess(*x, x->mBuffer).display();
    }
    
    // Frame method
    
    void frame()
    {
        const MaxConnection connection = mGlobal->getConnection();
        
        long index = getInlet() - getNumAudioIns();
        
        if (!connection.mObject)
            return;
        
        if (mGlobal->getConnectionMode() == ConnectionMode::kConnect)
        {
            connect(connection, index);
        }
        else if (mConfirmation)
        {
            if (mConfirmation->confirm(connection, index) && mGlobal->getConnectionMode() == ConnectionMode::kDoubleCheck)
                object_error(mUserObject, "extra connection to input %ld", index + 1);
        }
    }
    
    // Get Audio Outputs
    
    std::vector<double *> &getAudioOuts()
    {
        return mSigOuts;
    }
    
    // External methods (A_CANT)
    
    static t_ptr_int extConnectionAccept(FrameLib_MaxClass *x, t_object *dst, long srcout, long dstin, t_object *op, t_object *ip)
    {
        return (t_ptr_int) x->connectionAccept(dst, srcout, dstin, op, ip);
    }
    
    static t_max_err extPatchLineUpdate(FrameLib_MaxClass *x, t_object *line, long type, t_object *src, long srcout, t_object *dst, long dstin)
    {
        return x->patchLineUpdate(line, type, src, srcout, dst, dstin);
    }

    static void extFindAudio(FrameLib_MaxClass *x, std::vector<FrameLib_MaxNRTAudio> objects)
    {
        objects.push_back(FrameLib_MaxNRTAudio(x->mObject.get(), x->mBuffer));
    }
    
    static void extResolveGraph(FrameLib_MaxClass *x, const FrameLib_Context &context)
    {
        if (context == x->mObject->getContext())
            x->resolveGraph();
    }
    
    static void extResolveConnections(FrameLib_MaxClass *x, t_ptr_int *flag)
    {
        *flag |= x->resolveConnections();
    }
    
    static void extMarkUnresolved(FrameLib_MaxClass *x)
    {
        x->mResolved = false;
    }
    
    static void extSetDSPObject(FrameLib_MaxClass *x, t_object *object)
    {
        x->mDSPObject = object;
    }
    
    static t_object *extGetDSPObject(FrameLib_MaxClass *x)
    {
        return x->mDSPObject;
    }
    
    static void extAutoOrderingConnections(FrameLib_MaxClass *x)
    {
        x->mObject->autoOrderingConnections();
    }
    
    static void extClearAutoOrderingConnections(FrameLib_MaxClass *x)
    {
        x->mObject->clearAutoOrderingConnections();
    }

    static void extReset(FrameLib_MaxClass *x, const double *samplerate, t_ptr_int maxvectorsize)
    {
        x->mObject->reset(*samplerate, maxvectorsize);
    }
    
    static void extConnectionUpdate(FrameLib_MaxClass *x, t_ptr_int state)
    {
        x->mConnectionsUpdated = state;
    }
    
    static FLObject *extGetFLObject(FrameLib_MaxClass *x)
    {
        return x->mObject.get();
    }
    
    static t_object *extGetUserObject(FrameLib_MaxClass *x)
    {
        return x->mUserObject;
    }
    
    static void extConnectionConfirm(FrameLib_MaxClass *x, unsigned long index, ConnectionMode mode)
    {
        x->makeConnection(index, mode);
    }
    
    static t_ptr_int extIsConnected(FrameLib_MaxClass *x, unsigned long index)
    {
        return (t_ptr_int) x->confirmConnection(index, ConnectionMode::kConfirm);
    }

private:
    
    // Attempt to match the context to that of a given framelib object
    
    void matchContext(t_object *object)
    {
        FrameLib_Context current = mObject->getContext();
        FrameLib_Context context = toFLObject(object)->getContext();
        unsigned long size =  0;
            
        if (handlesAudio() || current == context || current.getReference() != context.getReference())
            return;
        
        mRealtime = !mRealtime;
        mResolved = false;
        
        mGlobal->pushToQueue(*this);

        FrameLib_Parameters::AutoSerial serialisedParams(*mObject->getSerialised());
        
        T *newObject = new T(context, &serialisedParams, mFrameLibProxy.get(), mSpecifiedStreams);
        
        for (unsigned long i = 0; i < getNumIns(); i++)
            if (const double *values = mObject->getFixedInput(i, &size))
                newObject->setFixedInput(i, values, size);
                
        if (isRealtime() && !mDSPObject)
            mDSPObject = objectMethod<t_object *>(object, gensym("__fl.get_dsp_object"));
        
        dspSetBroken(mDSPObject);
        mDSPObject = nullptr;
        
        mObject.reset(newObject);
    }
    
    // Get the association of a patch
    
    static t_object *getAssociation(t_object *patch)
    {
        t_object *assoc = 0;
        objectMethod(patch, gensym("getassoc"), &assoc);
        return assoc;
    }
    
    // Graph methods
    
    template <typename...Args>
    void traversePatch(t_patcher *p, t_object *contextAssoc, t_symbol *theMethod, Args...args)
    {
        t_object *assoc = getAssociation(p);
        
        // Avoid recursion into a poly / pfft / etc. - If the subpatcher is a wrapper we do need to deal with it
        
        if (assoc != contextAssoc && !objectMethod(assoc, gensym("__fl.wrapper_is_wrapper")))
            return;
        
        // Search for subpatchers, and call method on objects that don't have subpatchers
        
        for (t_box *b = jpatcher_get_firstobject(p); b; b = jbox_get_nextobject(b))
        {
            long index = 0;
            
            while (b && (p = (t_patcher *)object_subpatcher(jbox_get_object(b), &index, this)))
                traversePatch(p, contextAssoc, theMethod, args...);
            
            FLObject *object = toFLObject(jbox_get_object(b));
            
            if (object && object->getContext() = mObject->getContext)
                objectMethod(jbox_get_object(b), theMethod, args...);
        }
    }
    
    template <typename...Args>
    void traversePatch(t_symbol *theMethod, Args...args)
    {
        // Clear the queue and after traversing call objects added to the queue
        
        mGlobal->clearQueue();
        
        traversePatch(mContextPatch, getAssociation(mContextPatch), theMethod, args...);
        
        while (t_object *object = mGlobal->popFromQueue())
            objectMethod(object, theMethod, args...);
    }
    
    bool resolveGraph()
    {
        if (isRealtime() && mDSPObject && sys_getdspobjdspstate(mDSPObject))
            return false;
        
        t_ptr_int updated = false;
        
        mGlobal->setReportContextErrors(true);
        traversePatch(gensym("__fl.mark_unresolved"));
        traversePatch(gensym("__fl.resolve_connections"), &updated);
        traversePatch(gensym("__fl.connection_update"), t_ptr_int(false));
        mGlobal->setReportContextErrors(false);

        // If updated then redo auto ordering connections
        
        if (updated)
        {
            traversePatch(gensym("__fl.clear_auto_ordering_connections"));
            traversePatch(gensym("__fl.auto_ordering_connections"));
            post("Graph Updated - realtime %d", isRealtime());
        }
        
        return updated;
    }
    
    void checkGraph(double sampleRate, bool forceReset)
    {
        bool updated = resolveGraph();
        
        if (updated || forceReset)
            traversePatch(gensym("__fl.reset"), &sampleRate, static_cast<t_ptr_int>(maxBlockSize()));
    }
    
    // Convert from framelib object to max object and vice versa
    
    static FLObject *toFLObject(t_object *x)
    {
        return objectMethod<FLObject *>(x, gensym("__fl.get_framelib_object"));
    }
    
    static t_object *toMaxObject(FLObject *object)
    {
        return object ? (dynamic_cast<FrameLib_MaxProxy *>(object->getProxy()))->mMaxObject : nullptr;
    }
    
    // Get the number of audio ins/outs safely from a generic pointer
    
    static long getNumAudioIns(t_object *x)
    {
        FLObject *object = toFLObject(x);
        return static_cast<long>(object ? object->getNumAudioIns() : 0);
    }
    
    static long getNumAudioOuts(t_object *x)
    {
        FLObject *object = toFLObject(x);
        return static_cast<long>(object ? object->getNumAudioOuts() : 0);
    }
    
    // Helpers for connection methods
    
    static bool isOrderingInput(long index, FLObject *object)
    {
        return object && object->supportsOrderingConnections() && index == (long) object->getNumIns();
    }
    
    bool isOrderingInput(long index) const                  { return isOrderingInput(index, mObject.get()); }
    bool isConnected(long index) const                      { return mObject->isConnected(index); }
    
    static bool validIO(long index, unsigned long count)    { return index >= 0 && index < (long) count; }
    static bool validInput(long index, FLObject *object)    { return object && validIO(index, object->getNumIns()); }
    static bool validOutput(long index, FLObject *object)   { return object && validIO(index, object->getNumOuts()); }
    
    bool validInput(long index) const                       { return validInput(index, mObject.get()); }
    bool validOutput(long index) const                      { return validOutput(index, mObject.get()); }
    
    MaxConnection getConnection(long index)                 { return toMaxConnection(mObject->getConnection(index)); }
    MaxConnection getOrderingConnection(long index)         { return toMaxConnection(mObject->getOrderingConnection(index)); }
    
    long getNumOrderingConnections() const                  { return (long) mObject->getNumOrderingConnections(); }
    
    static MaxConnection toMaxConnection(FLConnection c)    { return MaxConnection(toMaxObject(c.mObject), c.mIndex); }
    static FLConnection toFLConnection(MaxConnection c)     { return FLConnection(toFLObject(c.mObject), c.mIndex); }
    
    // Private connection methods

    bool resolveConnections()
    {
        if (mResolved)
            return false;

        // Confirm input connections
        
        for (long i = 0; i < getNumIns(); i++)
            confirmConnection(i, ConnectionMode::kConfirm);
        
        // Confirm ordering connections
        
        for (long i = 0; i < getNumOrderingConnections(); i++)
            confirmConnection(getOrderingConnection(i), getNumIns(), ConnectionMode::kConfirm);
        
        // Make output connections
        
        for (long i = getNumOuts(); i > 0; i--)
            makeConnection(i - 1, ConnectionMode::kConnect);
        
        // Check if anything has updated since the last call to this method and make realtime resolved
        
        bool updated = mConnectionsUpdated;
        mConnectionsUpdated = false;
        mResolved = true;

        return updated;
    }

    void makeConnection(unsigned long index, ConnectionMode mode)
    {
        mGlobal->setConnection(MaxConnection(*this, index), mode);
        outlet_anything(mOutputs[index], gensym("frame"), 0, nullptr);
        mGlobal->clearConnection();
    }
    
    bool confirmConnection(unsigned long inIndex, ConnectionMode mode)
    {
        return confirmConnection(getConnection(inIndex), inIndex, mode);
    }
    
    bool confirmConnection(MaxConnection connection, unsigned long inIndex, ConnectionMode mode)
    {
        if (!validInput(inIndex) || !connection.mObject)
            return false;
        
        ConnectionConfirmation confirmation(connection, inIndex);
        mConfirmation = &confirmation;
        objectMethod(connection.mObject, gensym("__fl.connection_confirm"), connection.mIndex, mode);
        mConfirmation = nullptr;
        
        if (!confirmation.mConfirm)
            disconnect(connection, inIndex);
        
        return confirmation.mConfirm;
    }
    
    void connect(MaxConnection connection, long inIdx)
    {
        ConnectionResult result;
        FLConnection internalConnection = toFLConnection(connection);
        
        if (!isOrderingInput(inIdx) && (!validInput(inIdx) || !validOutput(connection.mIndex, internalConnection.mObject) || getConnection(inIdx) == connection || confirmConnection(inIdx, ConnectionMode::kDoubleCheck)))
            return;
        
        matchContext(connection.mObject);

        if (isOrderingInput(inIdx))
            result = mObject->addOrderingConnection(internalConnection);
        else
            result = mObject->addConnection(internalConnection, inIdx);

        switch (result)
        {
            case kConnectSuccess:
                mConnectionsUpdated = true;
                objectMethod(connection.mObject, gensym("__fl.connection_update"), t_ptr_int(true));
                break;
                
            case kConnectFeedbackDetected:
                object_error(mUserObject, "feedback loop detected");
                break;
                
            case kConnectWrongContext:
                if (mGlobal->getReportContextErrors())
                    object_error(mUserObject, "can't connect objects in different patching contexts");
                break;
                
            case kConnectSelfConnection:
                object_error(mUserObject, "direct feedback loop detected");
                break;
                
            case kConnectNoOrderingSupport:
            case kConnectAliased:
                break;
        }
    }
    
    void disconnect(MaxConnection connection, long inIdx)
    {
        if (!isOrderingInput(inIdx) && (!validInput(inIdx) || getConnection(inIdx) != connection))
            return;
        
        if (isOrderingInput(inIdx))
            mObject->deleteOrderingConnection(toFLConnection(connection));
        else
            mObject->deleteConnection(inIdx);
        
        mConnectionsUpdated = true;
    }

    // Patchline connections
    
    void unwrapConnection(t_object *& object, long& connection)
    {
        t_object *wrapped = objectMethod<t_object *>(object, gensym("__fl.wrapper_unwrap"), &connection);
        object = wrapped ? wrapped : object;
    }
    
    t_max_err patchLineUpdate(t_object *line, long type, t_object *src, long srcout, t_object *dst, long dstin)
    {
        if (*this != dst || type == JPATCHLINE_ORDER)
            return MAX_ERR_NONE;
        
        // Unwrap and offset connections
            
        unwrapConnection(src, srcout);
        srcout -= getNumAudioOuts(src);
        dstin -= getNumAudioIns();
        
        if (!isOrderingInput(dstin) && !validInput(dstin))
            return MAX_ERR_NONE;

        if (!isRealtime() || !dspSetBroken(mDSPObject))
        {
            FLObject *object = toFLObject(src);

            if (object && object->getContext() != mObject->getContext())
            {
                if (object->getType() == kScheduler || object->getNumAudioChans())
                    mGlobal->addContextToResolve(object->getContext(), src);
            }
            else
            {
                if (type == JPATCHLINE_CONNECT)
                    connect(MaxConnection(src, srcout), dstin);
                else
                    disconnect(MaxConnection(src, srcout), dstin);
            }
        }
        
        return MAX_ERR_NONE;
    }
    
    long connectionAccept(t_object *dst, long srcout, long dstin, t_object *op, t_object *ip)
    {
        t_symbol *className = object_classname(dst);
        
        // Allow if connecting to an outlet or patcher
        
        if (className == gensym("outlet") || className == gensym("jpatcher"))
            return 1;

        // Unwrap and offset connections

        unwrapConnection(dst, dstin);
        dstin -= getNumAudioIns(dst);
        srcout -= getNumAudioOuts();
        
        // Allow connections - if not a frame outlet / to the ordering inlet / to a valid unconnected input
        
        if (!validOutput(srcout) || isOrderingInput(dstin, toFLObject(dst)) || (validInput(dstin, toFLObject(dst)) && !objectMethod(dst, gensym("__fl.is_connected"), dstin)))
            return 1;
        
        return 0;
    }

    // Info Utilities
    
    void postSplit(const char *text, const char *firstLineTag, const char *lineTag)
    {
        std::string str(text);
        size_t prev = 0;
        
        for (size_t pos = str.find_first_of(":."); prev < str.size(); pos = str.find_first_of(":.", pos + 1))
        {
            pos = pos == std::string::npos ? str.size() : pos;
            object_post(mUserObject, "%s%s", prev ? lineTag : firstLineTag, str.substr(prev, (pos - prev) + 1).c_str());
            prev = pos + 1;
        }
    }
    
    const char *frameTypeString(FrameType type)
    {
        switch (type)
        {
            case kFrameNormal:          return "vector";
            case kFrameTagged:          return "tagged";
            // kFrameAny
            default:                    return "either";
        }
    }
    
    // Parameter Parsing
    
    unsigned long safeCount(char *str, unsigned long maxCount)
    {
        unsigned long number = std::max(1, atoi(str));
        return std::min(maxCount, number);
    }
    
    long getStreamCount(t_atom *a)
    {
        if (atom_gettype(a) == A_SYM)
        {
            t_symbol *sym = atom_getsym(a);
            
            if (strlen(sym->s_name) > 1 && sym->s_name[0] == '=')
                return safeCount(sym->s_name + 1, 1024);
        }
        
        return 0;
    }
    
    bool isParameterTag(t_symbol *sym)
    {        
        return strlen(sym->s_name) > 1 && sym->s_name[0] == '/';
    }
    
    bool isInputTag(t_symbol *sym)
    {
        size_t len = strlen(sym->s_name);
        
        if (len > 2)
            return (sym->s_name[0] == '[' && sym->s_name[len - 1] == ']');
        
        return false;
    }
    
    bool isTag(t_atom *a)
    {
        t_symbol *sym = atom_getsym(a);
        return isParameterTag(sym) || isInputTag(sym);
    }
    
    long parseNumericalList(std::vector<double> &values, t_atom *argv, long argc, long idx)
    {
        values.resize(0);
        
        // Collect doubles
        
        for ( ; idx < argc; idx++)
        {
            if (isTag(argv + idx))
                break;
            
            if (atom_gettype(argv + idx) == A_SYM && !atom_getfloat(argv + idx))
                object_error(mUserObject, "string %s in entry list where value expected", atom_getsym(argv + idx)->s_name);
            
            values.push_back(atom_getfloat(argv + idx));
        }
        
        return idx;
    }
    
    void parseParameters(FrameLib_Parameters::AutoSerial& serialisedParameters, long argc, t_atom *argv)
    {
        t_symbol *sym = nullptr;
        std::vector<double> values;
        long i;
        
        // Parse arguments
        
        for (i = 0; i < argc; i++)
        {
            if (isTag(argv + i))
                break;
            
            if (argsMode == kAsParams)
            {
                char argNames[64];
                sprintf(argNames, "%ld", i);
                
                if (atom_gettype(argv + i) == A_SYM)
                {
                    t_symbol *str = atom_getsym(argv + i);
                    serialisedParameters.write(argNames, str->s_name);
                }
                else
                {
                    double value = atom_getfloat(argv + i);
                    serialisedParameters.write(argNames, &value, 1);
                }
            }
        }
        
        // Parse parameters
        
        while (i < argc)
        {
            // Strip stray items
            
            for (long j = 0; i < argc; i++, j++)
            {
                if (isTag(argv + i))
                {
                    sym = atom_getsym(argv + i);
                    break;
                }
                
                if (j == 0)
                    object_error(mUserObject, "stray items after entry %s", sym->s_name);
            }
            
            // Check for lack of values or end of list
            
            if ((++i >= argc) || isTag(argv + i))
            {
                if (i < (argc + 1))
                    object_error(mUserObject, "no values given for entry %s", sym->s_name);
                continue;
            }
            
            if (isParameterTag(sym))
            {
                // Do strings or values
                
                if (atom_getsym(argv + i) != gensym(""))
                    serialisedParameters.write(sym->s_name + 1, atom_getsym(argv + i++)->s_name);
                else
                {
                    i = parseNumericalList(values, argv, argc, i);
                    serialisedParameters.write(sym->s_name + 1, values.data(), static_cast<unsigned long>(values.size()));
                }
            }
            else
            {
                // Advance to next tag
                
                for ( ; i < argc && !isTag(argv + i); i++);
            }
                
        }
    }
    
    // Input Parsing
    
    unsigned long inputNumber(t_symbol *sym)
    {
        return safeCount(sym->s_name + 1, 16384) - 1;
    }
    
    void parseInputs(long argc, t_atom *argv)
    {
        std::vector<double> values;
        long i = 0;
        
        // Parse arguments if used to set inputs
        
        if (argsMode == kAllInputs || argsMode == kDistribute)
        {
            i = parseNumericalList(values, argv, argc, 0);
            if (argsMode == kAllInputs)
            {
                for (long j = 0; i && j < getNumIns(); j++)
                    mObject->setFixedInput(j, values.data(), static_cast<unsigned long>(values.size()));
            }
            else
            {
                for (long j = 0; j < i && (j + 1) < getNumIns(); j++)
                    mObject->setFixedInput(j + 1, &values[j], 1);
            }
        }
        
        // Parse tags
        
        while (i < argc)
        {
            // Advance to next input tag
            
            for ( ; i < argc && !isInputTag(atom_getsym(argv + i)); i++);
            
            // If there are values to read then do so
            
            if ((i + 1) < argc && !isTag(argv + i + 1))
            {
                t_symbol *sym = atom_getsym(argv + i);
                i = parseNumericalList(values, argv, argc, i + 1);
                mObject->setFixedInput(inputNumber(sym), values.data(), static_cast<unsigned long>(values.size()));
            }
            
            if ((i + 1) >= argc)
                break;
        }
    }

    // Buffer access (read and write multichannel buffers)
    
    void read(t_symbol *buffer, double **outs, size_t numChans, size_t size, size_t offset)
    {
        MaxBufferAccess access(*this, buffer);
        
        for (size_t i = 0; i < numChans; i++)
            access.read(outs[i], size, offset, i);
    }
    
    void write(t_symbol *buffer, const double * const *ins, size_t numChans, size_t size, size_t offset)
    {
        MaxBufferAccess access(*this, buffer);
        
        for (size_t i = 0; i < numChans; i++)
            access.write(ins[i], size, offset, i);
    }

protected:
    
    // Proxy
    
    std::unique_ptr<FrameLib_MaxProxy> mFrameLibProxy;
    
private:
    
    // Data - N.B. - the order is crucial for safe deconstruction
    
    FrameLib_MaxGlobals::ManagedPointer mGlobal;
    FrameLib_MaxGlobals::SyncCheck mSyncChecker;
    
    std::unique_ptr<FLObject> mObject;
    
    std::vector<unique_object_ptr> mInputs;
    std::vector<void *> mOutputs;
    std::vector<double *> mSigOuts;

    long mProxyNum;
    ConnectionConfirmation *mConfirmation;

    unique_object_ptr mSyncIn;
    
    t_object *mContextPatch;
    t_object *mUserObject;
    t_object *mDSPObject;
    
    unsigned long mSpecifiedStreams;

    bool mRealtime;
    bool mConnectionsUpdated;
    bool mResolved;
    
public:
    
    // Attribute
    
    t_symbol *mBuffer;
};

// Convenience for Objects Using FrameLib_Expand (use FrameLib_MaxClass_Expand<T>::makeClass() to create)

template <class T, MaxObjectArgsMode argsSetAllInputs = kAsParams>
using FrameLib_MaxClass_Expand = FrameLib_MaxClass<FrameLib_Expand<T>, argsSetAllInputs>;

#endif
