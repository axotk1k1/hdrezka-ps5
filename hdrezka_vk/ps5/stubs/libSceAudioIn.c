/*
 * PS5 Vulkan Template - the microphone's imports: a link stub for
 * libSceAudioIn.sprx, which the payload SDK ships none for. Only the names
 * matter: tools/link-title.sh links it as a shared object named
 * libSceAudioIn.sprx, so the title imports these from the system's module,
 * as it imports libSceAudioOut's through the SDK's own stub. Written as the
 * SDK's sce_stubs are.
 *
 * SPDX-License-Identifier: MIT
 */

__asm__(".global sceAudioInOpen\n"
    ".type sceAudioInOpen @function\n"
    "sceAudioInOpen:\n");

__asm__(".global sceAudioInInput\n"
    ".type sceAudioInInput @function\n"
    "sceAudioInInput:\n");

__asm__(".global sceAudioInClose\n"
    ".type sceAudioInClose @function\n"
    "sceAudioInClose:\n");
