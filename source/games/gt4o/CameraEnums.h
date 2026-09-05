#pragma once

/*
    Onboard camera mount points. Gran Turismo 4 only - this used to share
    a header with FileError, which put it in front of every file-device
    translation unit for no reason.
*/
typedef enum CameraOnboardMount
{
    CameraOnboardMount_DEFAULT = 0, /*!< 0 */
    CameraOnboardMount_DRIVER = 0, /*!< 0 */
    CameraOnboardMount_CHASE = 1, /*!< 1 */
    CameraOnboardMount_MIRROR_L = 3, /*!< 3 */
    CameraOnboardMount_MIRROR_R = 4, /*!< 4 */
    CameraOnboardMount_NOSE = 5, /*!< 5 */
    CameraOnboardMount_BONNET = 6, /*!< 6 */
    CameraOnboardMount_ROOF = 7, /*!< 7 */
    CameraOnboardMount_BACK = 8, /*!< 8 */
    CameraOnboardMount_TAIL = 9, /*!< 9 */
    CameraOnboardMount_SIDE_L = 10, /*!< 10 */
    CameraOnboardMount_SIDE_R = 11, /*!< 11 */
    CameraOnboardMount_FENDER_L = 12, /*!< 12 */
    CameraOnboardMount_FENDER_R = 13, /*!< 13 */
    CameraOnboardMount_WHEEL_FL = 14, /*!< 14 */
    CameraOnboardMount_WHEEL_FR = 15, /*!< 15 */
    CameraOnboardMount_WHEEL_RL = 16, /*!< 16 */
    CameraOnboardMount_WHEEL_RR = 17, /*!< 17 */
    CameraOnboardMount_OPTION_1 = 18, /*!< 18 */
    CameraOnboardMount_OPTION_2 = 19, /*!< 19 */
    CameraOnboardMount_METER = 20, /*!< 20 */
    CameraOnboardMount_CHAR1 = 21, /*!< 21 */
    CameraOnboardMount_CHAR2 = 22, /*!< 22 */
    CameraOnboardMount_CHAR3 = 23, /*!< 23 */
    CameraOnboardMount_CHAR4 = 24, /*!< 24 */
    CameraOnboardMount_DRIVER2 = 25, /*!< 25 */
} CameraOnboardMount;
