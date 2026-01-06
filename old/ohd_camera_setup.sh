#!/bin/bash

determine_platform_and_board_type() {
    uname_output=$(uname -a)
    kernel_version=$(echo "$uname_output" | awk '{print $3}')
    kernel_type=$(echo "$kernel_version" | awk -F '-' '{print $2}')

    case "$kernel_type" in
        "v7l+")
            board_type="rpi_4_"
            platform="rpi"
            supported_platform=true
            ;;
        "v7+")
            board_type="rpi_3_"
            platform="rpi"
            supported_platform=true
            ;;
        *)
            if echo "$kernel_version" | grep -q "rk356"; then
                board_type="rk3566"
                platform="rockchip"
                supported_platform=true
            elif echo "$kernel_version" | grep -q "radxa"; then
                board_type="rk3588"
                platform="rockchip"
                supported_platform=true
            elif echo "$kernel_version" | grep -q "x20"; then
                board_type="x20"
                platform="openhd"
                supported_platform=true
            else
                supported_platform=false
            fi
            ;;
    esac
}

read_config_file() {
    if [ $# -eq 0 ]; then
        if [[ "$board_type" == "rk3566" ]] || [[ "$board_type" == "rk3588" ]]; then
            config_file="/config/openhd/camera1.txt"
        else
            config_file="/boot/openhd/camera1.txt"
        fi
        config_file_content=$(<$config_file)
    else
        config_file_content=$1
    fi
}

set_camera_type() {
    case $config_file_content in
        0) cam_type="X_CAM_TYPE_DUMMY_SW"; cma=false ;;
        1) cam_type="X_CAM_TYPE_USB"; cma=false ;;
        2) cam_type="X_CAM_TYPE_EXTERNAL"; cma=false ;;
        3) cam_type="X_CAM_TYPE_EXTERNAL_IP"; cma=false ;;
        4) cam_type="X_CAM_TYPE_DEVELOPMENT_FILESRC"; cma=false ;;
        20) cam_type="X_CAM_TYPE_RPI_MMAL_HDMI_TO_CSI"; cam_link="fkms"; cma=false ;;
        30) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_RPIF_V1_OV5647"; cam_link="kms"; cam_ident="ov5647"; cma=false ;;
        31) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_RPIF_V2_IMX219"; cam_link="kms"; cam_ident="imx219"; cma=false ;;
        32) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_RPIF_V3_IMX708"; cam_link="kms"; cam_ident="imx708"; cma=false ;;
        33) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_RPIF_HQ_IMX477"; cam_link="kms"; cam_ident="imx477"; cma=false ;;
        40) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_SKYMASTERHDR"; cam_link="kms"; cam_ident="imx708"; cma=true ;;
        41) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_SKYVISIONPRO"; cam_link="kms"; cam_ident="imx519"; cma=true ;;
        42) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_IMX477M"; cam_link="kms"; cam_ident="imx477"; cma=true ;;
        43) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_IMX462"; cam_link="kms"; cam_ident="imx462"; cma=true ;;
        44) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_IMX327"; cam_link="kms"; cam_ident="imx327"; cma=true ;;
        45) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_IMX290"; cam_link="kms"; cam_ident="arducam-pivariety"; cma=true ;;
        46) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_IMX462_LOWLIGHT_MINI"; cam_link="kms"; cam_ident="arducam-pivariety"; cma=true ;;
        47) cam_type="X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_IMX662"; cam_link="kms"; cam_ident="imx662"; cma=true ;;
        60) cam_type="X_CAM_TYPE_RPI_V4L2_VEYE_2MP"; cam_link="kms"; cam_ident="veyecam2m-overlay"; cma=false ;;
        61) cam_type="X_CAM_TYPE_RPI_V4L2_VEYE_CSIMX307"; cam_link="kms"; cam_ident="csimx307-overlay"; cma=false ;;
        62) cam_type="X_CAM_TYPE_RPI_V4L2_VEYE_CSSC132"; cam_link="kms"; cam_ident="cssc132-overlay"; cma=false ;;
        63) cam_type="X_CAM_TYPE_RPI_V4L2_VEYE_MVCAM"; cam_link="kms"; cam_ident="veye_mvcam-overlay"; cma=false ;;
        70) cam_type="X_CAM_TYPE_X20_HDZERO_GENERIC" ;;
        71) cam_type="X_CAM_TYPE_X20_HDZERO_RUNCAM_V1" ;;
        72) cam_type="X_CAM_TYPE_X20_HDZERO_RUNCAM_V2" ;;
        73) cam_type="X_CAM_TYPE_X20_HDZERO_RUNCAM_V3" ;;
        74) cam_type="X_CAM_TYPE_X20_HDZERO_RUNCAM_NANO_90" ;;
        75) cam_type="X_CAM_TYPE_X20_OHD_Jaguar" ;;
        80) cam_type="X_CAM_TYPE_ROCK_5_HDMI_IN"; cam_ident="rock-5b-hdmi1-8k" ;;
        81) cam_type="X_CAM_TYPE_ROCK_5_OV5647"; cam_ident="rpi-camera-v1_3" ;;
        82) cam_type="X_CAM_TYPE_ROCK_5_IMX219"; cam_ident="rpi-camera-v2" ;;
        83) cam_type="X_CAM_TYPE_ROCK_5_IMX708"; cam_ident="imx708" ;;
        84) cam_type="X_CAM_TYPE_ROCK_5_IMX462"; cam_ident="arducam-pivariety" ;;
        85) cam_type="X_CAM_TYPE_ROCK_5_IMX415"; cam_ident="imx415" ;;
        86) cam_type="X_CAM_TYPE_ROCK_5_IMX477"; cam_ident="arducam-pivariety" ;;
        87) cam_type="X_CAM_TYPE_ROCK_5_IMX519"; cam_ident="arducam-pivariety" ;;
        88) cam_type="X_CAM_TYPE_ROCK_5_OHD_Jaguar"; cam_ident="ohd-jaguar" ;;
        90) cam_type="X_CAM_TYPE_ROCK_3_HDMI_IN"; cam_ident="hdmi-in" ;;
        91) cam_type="X_CAM_TYPE_ROCK_3_OV5647"; cam_ident="rpi-camera-v1.3" ;;
        92) cam_type="X_CAM_TYPE_ROCK_3_IMX219"; cam_ident="rpi-camera-v2" ;;
        93) cam_type="X_CAM_TYPE_ROCK_3_IMX708"; cam_ident="imx708" ;;
        94) cam_type="X_CAM_TYPE_ROCK_3_IMX462"; cam_ident="arducam-pivariety-imx462" ;;
        95) cam_type="X_CAM_TYPE_ROCK_3_IMX519"; cam_ident="arducam-pivariety-imx519" ;;
        96) cam_type="X_CAM_TYPE_ROCK_3_OHD_Jaguar"; cam_ident="ohd-jaguar" ;;
        101) cam_type="X_CAM_TYPE_NVIDIA_XAVIER_IMX577" ;;
        110) cam_type="X_CAM_TYPE_OPENIPC_GENERIC" ;;
        *) cam_type="Unknown"; cam_link="unknown_link"; cma=false ;;
    esac
}

handle_rpi_platform() {
    if [[ "$cam_type" == "X_CAM_TYPE_RPI_LIBCAMERA_ARDUCAM_IMX477M" ]]; then
        if [ -e /usr/share/libcamera/ipa/rpi/vc4/imx477_old.json ]; then
            echo "No Custom Tuning needed!"
        else
            mv /usr/share/libcamera/ipa/rpi/vc4/imx477.json /usr/share/libcamera/ipa/rpi/vc4/imx477_old.json
            cp /usr/share/libcamera/ipa/rpi/vc4/arducam-477m.json /usr/share/libcamera/ipa/rpi/vc4/imx477.json
        fi
    elif [[ "$cam_type" == "X_CAM_TYPE_RPI_LIBCAMERA_RPIF_HQ_IMX477" ]]; then
        if [ -e /usr/share/libcamera/ipa/raspberrypi/imx477_old.json ]; then
            rm /usr/share/libcamera/ipa/rpi/vc4/imx477.json
            mv /usr/share/libcamera/ipa/rpi/vc4/imx477_old.json /usr/share/libcamera/ipa/rpi/vc4/imx477.json
            rm /usr/share/libcamera/ipa/rpi/vc4/imx477_old.json
        else
            echo "No Custom Tuning needed!"
        fi
    fi

    # Preparing everything
    echo "Camera Type: $cam_type"
    echo "Current Config:" 
    cp /boot/config.txt /boot/config.txt.bak
    sed -i '/^dtoverlay=gpio-key/d' /boot/config.txt
    grep '^dtoverlay' /boot/config.txt
    sed -i '/#OPENHD_DYNAMIC_CONTENT_BEGIN#/q' /boot/config.txt

    # Create Overlay
    [[ "$cma" == true ]] && append=",cma=400M" || append=""
    
    if [[ "$board_type" == "rpi_4_" ]]; then
        dtoverlayL1="dtoverlay=vc4-$cam_link-v3d${append}"
    elif [[ "$board_type" == "rpi_3_" ]]; then
        dtoverlayL1="dtoverlay=vc4-fkms-v3d${append}"
    fi

    echo "$dtoverlayL1" >> /boot/config.txt
    dtoverlayL2="dtoverlay=$cam_ident"
    echo "$dtoverlayL2" >> /boot/config.txt

    # Debug message
    echo "$dtoverlayL1"
    echo "$dtoverlayL2"
    touch /boot/openhd/camera.txt
}

handle_rockchip_platform() {
    if [[ "$board_type" == "rk3566" ]]; then
        echo "This Platform is Rockchip based and a RK3566 SOC"
        if apt list --installed | grep -q "u-boot-radxa-zero3"; then
            rk_config_spacer="        fdtoverlays  "
            rk_config_platform="radxa-zero3-"
            rk_config_line="${rk_config_spacer}${rk_config_platform}${cam_ident}.dtbo"
            # Search for lines containing "fdtoverlays" in the extlinux.conf file
            lines_old=$(grep -n "fdtoverlays" /boot/extlinux/extlinux.conf | cut -d':' -f1)
            for line in $lines_old; do
                awk 'NR != '$line' { print }' /boot/extlinux/extlinux.conf > /boot/extlinux/extlinux.conf.tmp && mv /boot/extlinux/extlinux.conf.tmp /boot/extlinux/extlinux.conf
            done
            # Search for lines containing "append" in the extlinux.conf file
            lines=$(grep -n "append" /boot/extlinux/extlinux.conf | cut -d':' -f1)
            for line in $lines; do
                awk -v line="$((line))" -v rk_config_line="$rk_config_line" 'NR == line {found=1; if (!($0 ~ rk_config_line)) print rk_config_line} {print} END {if (!found) print rk_config_line}' /boot/extlinux/extlinux.conf > tmpfile && mv tmpfile /boot/extlinux/extlinux.conf
            done
            sudo u-boot-update
            # Copy the overlay to the correct position
            rk_config_overlay_path="/boot/dtbo/"
            rk_config_overlay_file=$rk_config_overlay_path$rk_config_platform$cam_ident".dtbo"
            rk_config_overlay_disabled=$rk_config_overlay_file".disabled"
            cp $rk_config_overlay_disabled $rk_config_overlay_file
        else
            echo "This is an unknown platform"
        fi
   elif [[ "$board_type" == "rk3588" ]]; then
    echo "This Platform is Rockchip based and a RK3588 SOC"
    echo "Checking for installed u-boot packages..."
    if apt list --installed | grep -q "u-boot-rock-5a"; then
        echo "u-boot-rock-5a is installed"
        rk_config_spacer="        fdtoverlays  "
        rk_config_platform="rock-5a-"
        rk_config_line="${rk_config_spacer}${rk_config_platform}${cam_ident}.dtbo"
    elif apt list --installed | grep -q "u-boot-rock-5b"; then
        echo "u-boot-rock-5b is installed"
        rk_config_spacer="        fdtoverlays  "
        rk_config_platform="rock-5b-"
        rk_config_line="${rk_config_spacer}${rk_config_platform}${cam_ident}.dtbo"
    else 
        echo "Image not supported"
        exit 1
    fi

    echo "Searching for lines containing 'fdtoverlays' in /boot/extlinux/extlinux.conf..."
    lines_old=$(grep -n "fdtoverlays" /boot/extlinux/extlinux.conf | cut -d':' -f1)
    echo "Lines with 'fdtoverlays': $lines_old"
    for line in $lines_old; do
        echo "Removing line $line from /boot/extlinux/extlinux.conf"
        awk 'NR != '$line' { print }' /boot/extlinux/extlinux.conf > /boot/extlinux/extlinux.conf.tmp && mv /boot/extlinux/extlinux.conf.tmp /boot/extlinux/extlinux.conf
    done

    echo "Searching for lines containing 'append' in /boot/extlinux/extlinux.conf..."
    lines=$(grep -n "append" /boot/extlinux/extlinux.conf | cut -d':' -f1)
    echo "Lines with 'append': $lines"
    for line in $lines; do
        echo "Processing line $line in /boot/extlinux/extlinux.conf"
        awk -v line="$((line))" -v rk_config_line="$rk_config_line" 'NR == line {found=1; if (!($0 ~ rk_config_line)) print rk_config_line} {print} END {if (!found) print rk_config_line}' /boot/extlinux/extlinux.conf > tmpfile && mv tmpfile /boot/extlinux/extlinux.conf
    done

    echo "Running u-boot-update..."
    sudo u-boot-update

    echo "Copying the overlay to the correct position..."
    rk_config_overlay_path="/boot/dtbo/"
    rk_config_overlay_file=$rk_config_overlay_path$rk_config_platform$cam_ident".dtbo"
    rk_config_overlay_disabled=$rk_config_overlay_file".disabled"
    echo "Source overlay file: $rk_config_overlay_disabled"
    echo "Destination overlay file: $rk_config_overlay_file"
    cp $rk_config_overlay_disabled $rk_config_overlay_file

else
    echo "This is an unknown platform"
fi

}

determine_platform_and_board_type
read_config_file "$@"
set_camera_type

if [[ "$supported_platform" == true ]]; then
    echo "$platform"
    echo "$board_type"
    echo "$cam_type"
    if [[ "$platform" == "rpi" ]]; then
        handle_rpi_platform
    elif [[ "$platform" == "rockchip" ]]; then
        handle_rockchip_platform
        handle_rockchip_platform //dirty fix for not working on the first try
    fi
else
    echo "This platform is not supported"
fi
