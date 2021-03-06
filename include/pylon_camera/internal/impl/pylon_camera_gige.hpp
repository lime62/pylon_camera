/******************************************************************************
 * Software License Agreement (BSD License)
 *
 * Copyright (C) 2016, Magazino GmbH. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the names of Magazino GmbH nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#ifndef PYLON_CAMERA_INTERNAL_GIGE_H_
#define PYLON_CAMERA_INTERNAL_GIGE_H_

#include <string>
#include <vector>

#include <pylon_camera/internal/pylon_camera.h>

#include <pylon/gige/BaslerGigEInstantCamera.h>

namespace pylon_camera
{

struct GigECameraTrait
{
    typedef Pylon::CBaslerGigEInstantCamera CBaslerInstantCameraT;
    typedef Basler_GigECameraParams::ExposureAutoEnums ExposureAutoEnums;
    typedef Basler_GigECameraParams::GainAutoEnums GainAutoEnums;
    typedef Basler_GigECameraParams::PixelFormatEnums PixelFormatEnums;
    typedef Basler_GigECameraParams::PixelSizeEnums PixelSizeEnums;
    typedef GenApi::IInteger AutoTargetBrightnessType;
    typedef GenApi::IInteger GainType;
    typedef int64_t AutoTargetBrightnessValueType;
    typedef Basler_GigECameraParams::ShutterModeEnums ShutterModeEnums;
    typedef Basler_GigECamera::UserOutputSelectorEnums UserOutputSelectorEnums;

    static inline AutoTargetBrightnessValueType convertBrightness(const int& value)
    {
        return value;
    }
};

typedef PylonCameraImpl<GigECameraTrait> PylonGigECamera;

template <>
bool PylonGigECamera::applyCamSpecificStartupSettings(const PylonCameraParameter& parameters)
{
    try
    {
        // Remove all previous settings (sequencer etc.)
        // Default Setting = Free-Running
        cam_->UserSetSelector.SetValue(Basler_GigECameraParams::UserSetSelector_Default);
        cam_->UserSetLoad.Execute();
        // UserSetSelector_Default overrides Software Trigger Mode !!
        cam_->TriggerSource.SetValue(Basler_GigECameraParams::TriggerSource_Software);
        cam_->TriggerMode.SetValue(Basler_GigECameraParams::TriggerMode_On);

        /* Thresholds for the AutoExposure Funcitons:
         *  - lower limit can be used to get rid of changing light conditions
         *    due to 50Hz lamps (-> 20ms cycle duration)
         *  - upper limit is to prevent motion blur
         */
        cam_->AutoExposureTimeAbsLowerLimit.SetValue(cam_->ExposureTimeAbs.GetMin());
        cam_->AutoExposureTimeAbsUpperLimit.SetValue(cam_->ExposureTimeAbs.GetMax());

        cam_->AutoGainRawLowerLimit.SetValue(cam_->GainRaw.GetMin());
        cam_->AutoGainRawUpperLimit.SetValue(cam_->GainRaw.GetMax());

        // The gain auto function and the exposure auto function can be used at the
        // same time. In this case, however, you must also set the
        // Auto Function Profile feature.
        // acA1920-40gm does not suppert Basler_GigECameraParams::GainSelector_AnalogAll
        // has Basler_GigECameraParams::GainSelector_All instead
        // cam_->GainSelector.SetValue(Basler_GigECameraParams::GainSelector_AnalogAll);

        if ( GenApi::IsAvailable(cam_->BinningHorizontal) &&
             GenApi::IsAvailable(cam_->BinningVertical) )
        {
            ROS_INFO_STREAM("Cam has binning range: x(hz) = ["
                    << cam_->BinningHorizontal.GetMin() << " - "
                    << cam_->BinningHorizontal.GetMax() << "], y(vt) = ["
                    << cam_->BinningVertical.GetMin() << " - "
                    << cam_->BinningVertical.GetMax() << "].");
        }
        else
        {
            ROS_INFO_STREAM("Cam does not support binning.");
        }

        ROS_INFO_STREAM("Cam has exposure time range: ["
                << cam_->ExposureTimeAbs.GetMin()
                << " - " << cam_->ExposureTimeAbs.GetMax()
                << "] measured in microseconds.");
        ROS_INFO_STREAM("Cam has gain range: ["
                << cam_->GainRaw.GetMin() << " - "
                << cam_->GainRaw.GetMax()
                << "] measured in decive specific units.");

        ROS_INFO_STREAM("Cam has gammma range: ["
                << cam_->Gamma.GetMin() << " - "
                << cam_->Gamma.GetMax() << "].");
        ROS_INFO_STREAM("Cam has pylon auto brightness range: ["
                << cam_->AutoTargetValue.GetMin() << " - "
                << cam_->AutoTargetValue.GetMax()
                << "] which is the average pixel intensity.");

        // raise inter-package delay (GevSCPD) for solving error:
        // 'the image buffer was incompletely grabbed'
        // also in ubuntu settings -> network -> options -> MTU Size
        // from 'automatic' to 3000 if card supports it
        // Raspberry PI has MTU = 1500, max value for some cards: 9000
        cam_->GevSCPSPacketSize.SetValue(parameters.mtu_size_);

        // http://www.baslerweb.com/media/documents/AW00064902000%20Control%20Packet%20Timing%20With%20Delays.pdf
        // inter package delay in ticks (? -> mathi said in nanosec) -> prevent lost frames
        // package size * n_cams + 5% overhead = inter package size
        // int n_cams = 1;
        // int inter_package_delay_in_ticks = n_cams * imageSize() * 1.05;
        cam_->GevSCPD.SetValue(parameters.inter_pkg_delay_);
    }
    catch ( const GenICam::GenericException &e )
    {
        ROS_ERROR_STREAM("Error applying cam specific startup setting for GigE cameras: "
                << e.GetDescription());
        return false;
    }
    return true;
}

template <>
bool PylonGigECamera::setupSequencer(const std::vector<float>& exposure_times,
                                     std::vector<float>& exposure_times_set)
{
    try
    {
        if ( GenApi::IsWritable(cam_->SequenceEnable) )
        {
            cam_->SequenceEnable.SetValue(false);
        }
        else
        {
            ROS_ERROR("Sequence mode not enabled.");
            return false;
        }

        cam_->SequenceAdvanceMode = Basler_GigECameraParams::SequenceAdvanceMode_Auto;
        cam_->SequenceSetTotalNumber = exposure_times.size();

        for ( std::size_t i = 0; i < exposure_times.size(); ++i )
        {
            // Set parameters for each step
            cam_->SequenceSetIndex = i;
            float reached_exposure;
            setExposure(exposure_times.at(i), reached_exposure);
            exposure_times_set.push_back(reached_exposure / 1000000.);
            cam_->SequenceSetStore.Execute();
        }

        // config finished
        cam_->SequenceEnable.SetValue(true);
    }
    catch ( const GenICam::GenericException &e )
    {
        ROS_ERROR("%s", e.GetDescription());
        return false;
    }
    return true;
}

template <>
GenApi::IFloat& PylonGigECamera::exposureTime()
{
    if ( GenApi::IsAvailable(cam_->ExposureTimeAbs) )
    {
        return cam_->ExposureTimeAbs;
    }
    else
    {
        throw std::runtime_error("Error while accessing ExposureTimeAbs in PylonGigECamera");
    }
}

template <>
GigECameraTrait::GainType& PylonGigECamera::gain()
{
    if ( GenApi::IsAvailable(cam_->GainRaw) )
    {
        return cam_->GainRaw;
    }
    else
    {
        throw std::runtime_error("Error while accessing GainRaw in PylonGigECamera");
    }
}

template <>
bool PylonGigECamera::setGamma(const float& target_gamma, float& reached_gamma)
{
    if ( !GenApi::IsAvailable(cam_->Gamma) )
    {
        ROS_ERROR_STREAM("Error while trying to set gamma: cam.Gamma NodeMap is"
               << " not available!");
        return false;
    }

    // for GigE cameras you have to enable gamma first
    if ( GenApi::IsAvailable(cam_->GammaEnable) )
    {
        cam_->GammaEnable.SetValue(true);
    }

    if ( GenApi::IsAvailable(cam_->GammaSelector) )
    {
        // set gamma selector to user, so that the gamma value has an influence
        try
        {
            cam_->GammaSelector.SetValue(Basler_GigECameraParams::GammaSelector_User);
        }
        catch ( const GenICam::GenericException &e )
        {
            ROS_ERROR_STREAM("An exception while setting gamma selector to"
                << " USER occurred: " << e.GetDescription());
            return false;
        }
    }

    try
    {
        float gamma_to_set = target_gamma;
        if ( gamma().GetMin() > gamma_to_set )
        {
            gamma_to_set = gamma().GetMin();
            ROS_WARN_STREAM("Desired gamma unreachable! Setting to lower limit: "
                                  << gamma_to_set);
        }
        else if ( gamma().GetMax() < gamma_to_set )
        {
            gamma_to_set = gamma().GetMax();
            ROS_WARN_STREAM("Desired gamma unreachable! Setting to upper limit: "
                                  << gamma_to_set);
        }
        gamma().SetValue(gamma_to_set);
        reached_gamma = currentGamma();
    }
    catch ( const GenICam::GenericException &e )
    {
        ROS_ERROR_STREAM("An exception while setting target gamma to "
                << target_gamma << " occurred: " << e.GetDescription());
        return false;
    }
    return true;
}

template <>
GenApi::IFloat& PylonGigECamera::autoExposureTimeLowerLimit()
{
    if ( GenApi::IsAvailable(cam_->AutoExposureTimeAbsLowerLimit) )
    {
        return cam_->AutoExposureTimeAbsLowerLimit;
    }
    else
    {
        throw std::runtime_error("Error while accessing AutoExposureTimeAbsLowerLimit in PylonGigECamera");
    }
}

template <>
GenApi::IFloat& PylonGigECamera::autoExposureTimeUpperLimit()
{
    if ( GenApi::IsAvailable(cam_->AutoExposureTimeAbsUpperLimit) )
    {
        return cam_->AutoExposureTimeAbsUpperLimit;
    }
    else
    {
        throw std::runtime_error("Error while accessing AutoExposureTimeAbsUpperLimit in PylonGigECamera");
    }
}

template <>
GigECameraTrait::GainType& PylonGigECamera::autoGainLowerLimit()
{
    if ( GenApi::IsAvailable(cam_->AutoGainRawLowerLimit) )
    {
        return cam_->AutoGainRawLowerLimit;
    }
    else
    {
        throw std::runtime_error("Error while accessing AutoGainRawLowerLimit in PylonGigECamera");
    }
}

template <>
GigECameraTrait::GainType& PylonGigECamera::autoGainUpperLimit()
{
    if ( GenApi::IsAvailable(cam_->AutoGainRawUpperLimit) )
    {
        return cam_->AutoGainRawUpperLimit;
    }
    else
    {
        throw std::runtime_error("Error while accessing AutoGainRawUpperLimit in PylonGigECamera");
    }
}

template <>
GenApi::IFloat& PylonGigECamera::resultingFrameRate()
{
    if ( GenApi::IsAvailable(cam_->ResultingFrameRateAbs) )
    {
        return cam_->ResultingFrameRateAbs;
    }
    else
    {
        throw std::runtime_error("Error while accessing ResultingFrameRateAbs in PylonGigECamera");
    }
}

template <>
GigECameraTrait::AutoTargetBrightnessType& PylonGigECamera::autoTargetBrightness()
{
    if ( GenApi::IsAvailable(cam_->AutoTargetValue) )
    {
        return cam_->AutoTargetValue;
    }
    else
    {
        throw std::runtime_error("Error while accessing AutoTargetValue in PylonGigECamera");
    }
}

template <>
std::string PylonGigECamera::typeName() const
{
    return "GigE";
}

}  // namespace pylon_camera

#endif  // PYLON_CAMERA_INTERNAL_GIGE_H_
