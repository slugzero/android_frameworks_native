//
// Copyright 2010 The Android Open Source Project
//
// The Display dispatcher.
//
#define LOG_TAG "DisplayDispatcher"

//#define LOG_NDEBUG 0

#include <cutils/log.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <cutils/properties.h>
#include "DisplaySemaphore.h"
#include "DisplayDispatcher.h"
#include <ui/DisplayCommand.h>
#include "SurfaceFlinger.h"


#define INDENT "  "
#define INDENT2 "    "

#define  MAX_FRAMEID                  2147483640

namespace android 
{
	void	DisplayDispatcherThread::enqueuebuf(int frameidx)
	{
		int   i;
		int   tmp;
		
		tmp = mFrameidx[frameidx];
		for(i = frameidx;i > 0;i--)
		{
			mFrameidx[i] = mFrameidx[i - 1];
		}
		
		mFrameidx[0] = tmp;
	}
	
    void DisplayDispatcherThread::setSrcBuf(int srcfb_id,int srcfb_offset)
    {
        mSrcfbid        = srcfb_id;
        mSrcfboffset    = srcfb_offset;
    }

    void DisplayDispatcherThread::signalEvent(int reason)
    {
    	if(reason != 0)
    	{
	    	ALOGV("signalEvent!\n");
	    	mStartReason 	= reason;
	        mSemaphore->up();
	        mStartTime		= systemTime()/1000;
    	}
    }

    void DisplayDispatcherThread::waitForEvent()
    {
    	//ALOGD("waitForEvent!\n");
        mSemaphore->down();
    }
    
    void DisplayDispatcherThread::resetEvent()
    {
    	//ALOGD("waitForEvent!\n");
        mSemaphore->reset();
    }
    
    void	DisplayDispatcherThread::setConvertBufId(int bufid)
	{
		Mutex::Autolock autoLock(mBufferLock);
			
		mConvertBufId = bufid;
	}
	
	void	DisplayDispatcherThread::setConvertBufParam(int handle,int width,int height,int format)
	{
		mWifiDisplayBufHandle 	= handle;
		mWifiDisplayBufWidth 	= width;
		mWifiDisplayBufHeight 	= height;
		mWifiDisplayBufFormat 	= format;
		mConvertBufId			= DISPLAYDISPATCH_MAXBUFNO - 1;
	}
    
    // --- InputDispatcherThread ---
    void DisplayDispatcherThread::LooperOnce()
    {
        int  writebufid;
        int  showbufid;
        int  ret;
        int  write_index;
        int  startreason;
        
        ALOGV("before waitForEvent!\n");
        waitForEvent();
        
		startreason = mStartReason;
		ALOGV("after waitForEvent! startreason = %d\n",startreason);
		
		if(startreason & DISPLAYDISPATCH_STARTSAME)
		{
			mDispDevice->request_modelock(mDispDevice);

			if(mFrameidx[DISPLAYDISPATCH_MAXBUFNO - 1] == mFbOffset)
			{
	        	writebufid  = mFrameidx[DISPLAYDISPATCH_MAXBUFNO - 2];
	
	            write_index = DISPLAYDISPATCH_MAXBUFNO - 2;
	    	}
	    	else
	    	{
	    		writebufid  = mFrameidx[DISPLAYDISPATCH_MAXBUFNO - 1];
	
	            write_index = DISPLAYDISPATCH_MAXBUFNO - 1;
	    	}
	    	
	    	//ALOGD("writebufid = %d\n",writebufid);
	
	        ret = mDispDevice->copysrcfbtodstfb(mDispDevice,mSrcfbid,1 - mSrcfboffset,1 - mSrcfbid,writebufid);
	        if(ret != 0)
	        {
	            //ALOGE("copy src fb failed!\n");
	
	            mDispDevice->release_modelock(mDispDevice);
	
	            return ;
	        }
	
	        enqueuebuf(write_index);
	
	        showbufid = mFrameidx[0];
	        
	        mFbOffset = showbufid;
	        
	        //ALOGD("showbufid = %d,mSrcfbid = %d\n",showbufid,mSrcfbid);
	
	        mDispDevice->pandisplay(mDispDevice,1 - mSrcfbid,showbufid);
	
	        mDispDevice->release_modelock(mDispDevice); 
		}
		
		if(startreason & DISPLAYDISPATCH_STARTCONVERT)
		{
			{
				Mutex::Autolock autoLock(mBufferLock);
				ALOGV("mWifiDisplayBufId mConvertBufId = %d\n",mConvertBufId);
				if(mConvertBufId == DISPLAYDISPATCH_NOCONVERTFB)
				{
					if(mFlinger != NULL)
		            {
		            	mFlinger->NotifyFBConverted_l(0,0,DISPLAYDISPATCH_NOCONVERTFB,mStartTime);
		        	}
					return ;
				}
			
				ret = mDispDevice->convertfb(mDispDevice,mSrcfbid,1 - mSrcfboffset,mWifiDisplayBufHandle,mConvertBufId,mWifiDisplayBufWidth,mWifiDisplayBufHeight,mWifiDisplayBufFormat);
		        if(ret != 0)
		        {
		            ALOGE("mWifiDisplayBufId copy src fb failed!\n");
		            if(mFlinger != NULL)
		            {
		            	mFlinger->NotifyFBConverted_l(0,0,DISPLAYDISPATCH_NOCONVERTFB,mStartTime);
		        	}
					
		            return ;
		        }
		        
		        writebufid = mConvertBufId;
		    }
		    
		    unsigned int show_buf_addr;
            unsigned int show_buf_yaddr;
            unsigned int show_buf_caddr;

            show_buf_addr = mDispDevice->getdispbufaddr(mDispDevice, mWifiDisplayBufHandle, writebufid,mWifiDisplayBufWidth,mWifiDisplayBufHeight,mWifiDisplayBufFormat);
			show_buf_yaddr = show_buf_addr;
            show_buf_caddr = show_buf_yaddr + mWifiDisplayBufWidth * mWifiDisplayBufHeight;
            
            if(mFlinger != NULL)
            {
            	mFlinger->NotifyFBConverted_l((unsigned int)show_buf_yaddr,(unsigned int)show_buf_caddr,writebufid,mStartTime);
        	}

            ALOGV("mConvertBufId = %d,show_buf_yaddr = %x,show_buf_caddr = %x\n",writebufid,show_buf_yaddr,show_buf_caddr);
		}
    }

    DisplayDispatcherThread::DisplayDispatcherThread(display_device_t*	mDevice,const sp<SurfaceFlinger>& flinger) :
            Thread(/*canCallJava*/ true), mDispDevice(mDevice) 
    {
    	for(int i = 0;i < DISPLAYDISPATCH_MAXBUFNO;i++)
    	{
    		mFrameidx[i] 		= i;
    	}
    	
        mSemaphore = new DisplaySemaphore(0);
        
        mFlinger   = flinger;
    }

    DisplayDispatcherThread::~DisplayDispatcherThread() 
    {
        
    }


    bool DisplayDispatcherThread::threadLoop() 
    {
        this->LooperOnce();
        return true;
    }

    DisplayDispatcher::DisplayDispatcher(const sp<SurfaceFlinger>& flinger)
    {
        int 				err;
        hw_module_t* 		module;
        status_t 			result;
		char property[PROPERTY_VALUE_MAX];
	    err = hw_get_module(DISPLAY_HARDWARE_MODULE_ID, (hw_module_t const**)&module);
	    if (err == 0) 
	    {
		    err = display_open(module, &mDevice);
	    } 
        else
        {
            ALOGW("hw_get display module Failed!\n");
        }

        mWifiDisplayBufHandle = 0;
        
        mFlinger   = flinger;

	   if (property_get("ro.display.switch", property, NULL) > 0) 
       {
	        if (atoi(property) == 1) 
	        {
	            mThread = new DisplayDispatcherThread(mDevice,mFlinger);
		        result = mThread->run("DisplayDispatcheR", PRIORITY_HIGHEST);
			    if (result) 
			    {
			        ALOGE("Could not start DisplayDispatcher thread due to error %d.", result);
			
			        mThread->requestExit();
			    }
			    
	            ALOGD("display dispatcher enabled");
	        }
	    }
	    else
	    {
	            ALOGW("display dispatcher disable");
	    }

        if (property_get("ro.wifidisplay.switch", property, NULL) > 0) //
        {
	        if (atoi(property) == 1) //
	        {
	            ALOGW("wifidisplay dispatcher enabled");
	            if(mDevice)
    			{
                    mWifiDisplayBufWidth      = mDevice->getdisplayparameter(mDevice,0,DISPLAY_FBWIDTH);
                    mWifiDisplayBufHeight     = mDevice->getdisplayparameter(mDevice,0,DISPLAY_FBHEIGHT);
                    mWifiDisplayBufFormat     = DISPLAY_FORMAT_PYUV420UVC;
                    ALOGD("mSendWidth = %d,mSendHeight = %d,mSendFormat = %d\n",mWifiDisplayBufWidth,mWifiDisplayBufHeight,mWifiDisplayBufFormat);
    				mWifiDisplayBufHandle  = mDevice->requestdispbuf(mDevice,mWifiDisplayBufWidth,mWifiDisplayBufHeight,mWifiDisplayBufFormat,DISPLAYDISPATCH_MAXBUFNO);
    				if(mWifiDisplayBufHandle == 0)
    				{
    					ALOGV("display device request buffer failed! width = %d,height = %d,format = %d\n",mWifiDisplayBufWidth,mWifiDisplayBufHeight,mWifiDisplayBufFormat);
    				}
    				
    				mThread->setConvertBufParam(mWifiDisplayBufHandle,mWifiDisplayBufWidth,mWifiDisplayBufHeight,mWifiDisplayBufFormat);
    			}
	        }
	        else
	        {
	            ALOGW("wifidisplay dispatcher disable");
	        }
	    }
	    else
	    {
	        ALOGW("wifidisplay dispatcher disable");
	    }
		    //LOGD("DisplayDispatcher createing err2 = %d!\n",err);  
    }

    DisplayDispatcher::~DisplayDispatcher()
    {
    }
	
	int DisplayDispatcher::setDisplayParameter(int displayno, int value0,int value1)
	{
		if(displayno == 0)
		{
			mDisplayType0 	= value0;
			mDisplayFormat0 = value1;
		}
		else
		{
			mDisplayType1 	= value0;
			mDisplayFormat1 = value1;
		}
		
		return  0;
	}
	
	
	int DisplayDispatcher::setDisplayMode(int mode)
	{
		if(mDevice)
		{
			struct display_modepara_t    disp_para;

			disp_para.d0type			= mDisplayType0;
    		disp_para.d1type			= mDisplayType1;
    		disp_para.d0format			= mDisplayFormat0;
    		disp_para.d1format			= mDisplayFormat1;
    		disp_para.d0pixelformat		= 0;
    		disp_para.d1pixelformat		= 0;
    		disp_para.masterdisplay		= 0;

			return  mDevice->setdisplaymode(mDevice,mode,&disp_para);
		}

		return  -1;
	}

	
	int DisplayDispatcher::startWifiDisplaySend(int displayno,int mode)
	{
		char property[PROPERTY_VALUE_MAX];
		
		if (property_get("ro.wifidisplay.switch", property, NULL) > 0) 
        {
	        if (atoi(property) == 1) 
	        {
	            mStartConvert = true;
	        }
	    }
	    
        return 0;    
	}
	
	int DisplayDispatcher::endWifiDisplaySend(int displayno)
	{
		  mStartConvert = false;
          return 0;        
	}
	
	int DisplayDispatcher::startWifiDisplayReceive(int displayno,int mode)
	{
          return 0;       
	}
    
	int DisplayDispatcher::endWifiDisplayReceive(int displayno)
	{
          return 0;    
	}
	
	int DisplayDispatcher::updateSendClient(int mode)
	{
          return 0;    
	}
	
	int	DisplayDispatcher::setConvertBufId(int bufid)
	{
		mThread->setConvertBufId(bufid);
		
		return 0;
	}
	
    int DisplayDispatcher::setDispProp(int cmd,int param0,int param1,int param2)		
    {
        switch(cmd)
        {
            case  DISPLAY_CMD_SETDISPPARA:
                return  setDisplayParameter(param0,param1,param2);

            case  DISPLAY_CMD_CHANGEDISPMODE:
                return  mDevice->changemode(mDevice,param0,param1,param2);

            case  DISPLAY_CMD_CLOSEDISP:
                return  mDevice->closedisplay(mDevice,param0);

            case  DISPLAY_CMD_OPENDISP:
                return  mDevice->opendisplay(mDevice,param0);

            case  DISPLAY_CMD_GETDISPCOUNT:
                return  mDevice->getdisplaycount(mDevice);

            case DISPLAY_CMD_GETDISPLAYMODE:
                return  mDevice->getdisplaymode(mDevice);

            case DISPLAY_CMD_GETDISPPARA:
                return  mDevice->getdisplayparameter(mDevice,param0,param1);

            case DISPLAY_CMD_GETHDMISTATUS:
                return  mDevice->gethdmistatus(mDevice);

            case DISPLAY_CMD_GETMASTERDISP:
                return  mDevice->getmasterdisplay(mDevice);

            case DISPLAY_CMD_GETMAXHDMIMODE:
                return  mDevice->gethdmimaxmode(mDevice);

            case DISPLAY_CMD_GETMAXWIDTHDISP:
                return  mDevice->getmaxwidthdisplay(mDevice);

            case DISPLAY_CMD_GETTVSTATUS:
                return  mDevice->gettvdacstatus(mDevice);

            case DISPLAY_CMD_SETMASTERDISP:
                return  mDevice->setmasterdisplay(mDevice,param0);

            case DISPLAY_CMD_SETDISPMODE:
                return  setDisplayMode(param0);

			case DISPLAY_CMD_SETBACKLIGHTMODE:
				return  mDevice->setdisplaybacklightmode(mDevice,param0);

		    case DISPLAY_CMD_SETORIENTATION:
		        return mDevice->setOrientation(mDevice,param0);

			case DISPLAY_CMD_ISSUPPORTHDMIMODE:
			    return mDevice->issupporthdmimode(mDevice,param0);

			case DISPLAY_CMD_SETAREAPERCENT:
			    return mDevice->setdisplayareapercent(mDevice,param0,param1);

			case DISPLAY_CMD_GETAREAPERCENT:
			    return mDevice->getdisplayareapercent(mDevice,param0);

			case DISPLAY_CMD_SETBRIGHT:
			    return mDevice->setdisplaybrightness(mDevice,param0,param1);

			case DISPLAY_CMD_GETBRIGHT:
			    return mDevice->getdisplaybrightness(mDevice,param0);

			case DISPLAY_CMD_SETCONTRAST:
			    return mDevice->setdisplaycontrast(mDevice,param0,param1);

			case DISPLAY_CMD_GETCONTRAST:
			    return mDevice->getdisplaycontrast(mDevice,param0);

			case DISPLAY_CMD_SETSATURATION:
			    return mDevice->setdisplaysaturation(mDevice,param0,param1);

			case DISPLAY_CMD_GETSATURATION:
			    return mDevice->getdisplaysaturation(mDevice,param0);

			case DISPLAY_CMD_SETHUE:
			    return mDevice->setdisplayhue(mDevice,param0,param1);

			case DISPLAY_CMD_GETHUE:
                return mDevice->getdisplayhue(mDevice,param0);
				
			case DISPLAY_CMD_STARTWIFIDISPLAYSEND:                                
				return  startWifiDisplaySend(param0,param1);	                          
			                          
			case DISPLAY_CMD_ENDWIFIDISPLAYSEND:                                  
            	return  endWifiDisplaySend(param0);                          
            	                          
            case DISPLAY_CMD_STARTWIFIDISPLAYRECEIVE:          	                  
            	return  startWifiDisplayReceive(param0,param1);                          
            	
            case DISPLAY_CMD_ENDWIFIDISPLAYRECEIVE:          	
            	return  endWifiDisplayReceive(param0);	
           
            case DISPLAY_CMD_UPDATESENDCLIENT:          	
            	return  updateSendClient(param0);
            
            case DISPLAY_CMD_SETCONVERTFBID:          	
            	return  setConvertBufId(param0);
            	
            	default:
                ALOGE("Display Cmd not Support!\n");
                return  -1;
        }
    }
    
    int DisplayDispatcher::getDispProp(int cmd,int param0,int param1)
    {
        switch(cmd)
        {
            case  DISPLAY_CMD_GETWIFIDISPLAYBUFHANDLE:
                return  mWifiDisplayBufHandle;

            case  DISPLAY_CMD_GETWIFIDISPLAYBUFWIDTH:
                return  mWifiDisplayBufWidth;

            case  DISPLAY_CMD_GETWIFIDISPLAYBUFHEIGHT:
                return  mWifiDisplayBufHeight;

            case  DISPLAY_CMD_GETWIFIDISPLAYBUFFORMAT:
                return  mWifiDisplayBufFormat;
                
            	default:
                ALOGE("Display Cmd not Support!\n");
                return  0;
        }
    }

    void DisplayDispatcher::startSwapBuffer()
    {
        int     master_bufid;
        int     master_display;
        int     mode;
        int     outputtype;
        int	    plugin;
        int		reason = 0;
        
        mode            = mDevice->getdisplaymode(mDevice);
        master_display  = mDevice->getmasterdisplay(mDevice);
        outputtype		= mDevice->getdisplayparameter(mDevice,1 - master_display,DISPLAY_OUTPUT_TYPE);
        if(outputtype == DISPLAY_DEVICE_HDMI)
        {
        	plugin      = mDevice->gethdmistatus(mDevice);
        	if(plugin  == 0)
        	{
        		mThread->resetEvent();
        		
        		return  ;
        	}
        }
        else if(outputtype == DISPLAY_DEVICE_TV)
        {
        	plugin      = mDevice->gettvdacstatus(mDevice);
        	if(plugin  == 0)
        	{
        		mThread->resetEvent();
        		
        		return  ;
        	}
        }
        
    	if(mode == DISPLAY_MODE_DUALSAME || mode == DISPLAY_MODE_DUALSAME_TWO_VIDEO || mode == DISPLAY_MODE_SINGLE_VAR_BE || mStartConvert == true)
        {
            master_bufid    = mDevice->getdisplaybufid(mDevice,master_display);

            mThread->setSrcBuf(master_display,master_bufid);
            if(mode == DISPLAY_MODE_DUALSAME || mode == DISPLAY_MODE_DUALSAME_TWO_VIDEO || mode == DISPLAY_MODE_SINGLE_VAR_BE)
            {
            	reason |= DISPLAYDISPATCH_STARTSAME;
            }
            
            if(mStartConvert == true)
	        {	
	        	reason |= DISPLAYDISPATCH_STARTCONVERT;
	    	}
	    	
	    	mThread->signalEvent(reason);
        }
    }

} // namespace android
