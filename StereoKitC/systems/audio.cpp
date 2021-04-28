#include "audio.h"
#include "../asset_types/sound.h"

#include "../sk_memory.h"
#include "../sk_math.h"
#include "../systems/platform/platform_utils.h"

#include "../libraries/miniaudio.h"
#include "../libraries/stref.h"
#include "../libraries/isac_spatial_sound.h"

#include <string.h>

namespace sk {

sound_inst_t      au_active_sounds[8];
float             au_mix_temp[4096];
matrix            au_head_transform;

ma_context        au_context        = {};
ma_decoder_config au_decoder_config = {};
ma_device_config  au_config         = {};
ma_device         au_device         = {};
ma_device         au_mic_device     = {};
sound_t           au_mic_sound      = {};
char             *au_mic_name       = nullptr;
bool              au_recording      = false;
#if defined(SK_OS_WINDOWS) || defined(SK_OS_WINDOWS_UWP)
IsacAdapter*      isac_adapter      = nullptr;
#endif

///////////////////////////////////////////

ma_uint32 read_and_mix_pcm_frames_f32(sound_inst_t &inst, float *output, ma_uint32 frame_count) {
	// The way mixing works is that we just read into a temporary buffer, 
	// then take the contents of that buffer and mix it with the contents of
	// the output buffer by simply adding the samples together.
	vec3      head_pos          = input_head()->position;
	ma_uint32 frame_cap         = _countof(au_mix_temp) / AU_CHANNEL_COUNT;
	ma_uint32 total_frames_read = 0;

	while (total_frames_read < frame_count) {
		ma_uint32 frames_remaining = frame_count - total_frames_read;
		ma_uint32 frames_to_read   = frame_cap;
		if (frames_to_read > frames_remaining) {
			frames_to_read = frames_remaining;
		}

		// Grab sound samples!
		ma_uint32 frames_read = 0;
		switch (inst.sound->type) {
		case sound_type_decode: {
			frames_read = (ma_uint32)ma_decoder_read_pcm_frames(&inst.sound->decoder, au_mix_temp, frames_to_read);
		} break;
		case sound_type_stream: {
			mtx_lock(&inst.sound->data_lock);
			frames_read = ring_buffer_read(&inst.sound->buffer, au_mix_temp, frames_to_read);
			mtx_unlock(&inst.sound->data_lock);
		} break;
		case sound_type_buffer: {
			frames_read = mini(frames_to_read, (uint32_t)(inst.sound->buffer.count - inst.sound->buffer.cursor));
			memcpy(au_mix_temp, inst.sound->buffer.data+inst.sound->buffer.cursor, frames_read * sizeof(float));
			inst.sound->buffer.cursor += frames_read;
		} break;
		}
		if (frames_read <= 1) break;

		// Mix the sound samples in
		float dist   = vec3_magnitude(inst.position - head_pos);
		float volume = fminf(1,(1.f / dist) * inst.volume);

		// Mix the frames together.
		for (ma_uint32 sample = 0; sample < frames_read*AU_CHANNEL_COUNT; ++sample) {
			int   i = total_frames_read * AU_CHANNEL_COUNT + sample;
			float s = au_mix_temp[sample] * volume;
			output[i] = fmaxf(-1, fminf(1, output[i] + s));
		}

		total_frames_read += frames_read;
		if (frames_read < frames_to_read) {
			break;  // Reached EOF.
		}
	}

	return total_frames_read;
}

///////////////////////////////////////////

void data_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
	float* output_f = (float*)output;

	for (uint32_t i = 0; i < _countof(au_active_sounds); i++) {
		if (au_active_sounds[i].sound == nullptr)
			continue;

		ma_uint32 framesRead = read_and_mix_pcm_frames_f32(au_active_sounds[i], output_f, frame_count);
		if (framesRead < frame_count && au_active_sounds[i].sound->type != sound_type_stream) {
			sound_release(au_active_sounds[i].sound);
			au_active_sounds[i].sound = nullptr;
		}
	}

	(void)input;
}

///////////////////////////////////////////

ma_uint32 readDataForIsac(sound_inst_t& inst, float* output, ma_uint32 frame_count, vec3* position, float* volume) {
	// Set the position and volume for this object. ISAC applies this directly for us
	*position = matrix_mul_point(au_head_transform, inst.position);
	*volume   = inst.volume;

	ma_uint32 frame_cap         = _countof(au_mix_temp) / AU_CHANNEL_COUNT;
	ma_uint32 total_frames_read = 0;

	while (total_frames_read < frame_count) {
		ma_uint32 frames_remaining = frame_count - total_frames_read;
		ma_uint32 frames_to_read = frame_cap;
		if (frames_to_read > frames_remaining) {
			frames_to_read = frames_remaining;
		}

		ma_uint32 frames_read = 0;
		switch (inst.sound->type) {
		case sound_type_decode: {
			frames_read = (ma_uint32)ma_decoder_read_pcm_frames(&inst.sound->decoder, au_mix_temp, frames_to_read);
		} break;
		case sound_type_stream: {
			mtx_lock(&inst.sound->data_lock);
			frames_read = ring_buffer_read(&inst.sound->buffer, au_mix_temp, frames_to_read);
			mtx_unlock(&inst.sound->data_lock);
		} break;
		case sound_type_buffer: {
			frames_read = mini(frames_to_read, (uint32_t)(inst.sound->buffer.count - inst.sound->buffer.cursor));
			memcpy(au_mix_temp, inst.sound->buffer.data+inst.sound->buffer.cursor, frames_read * sizeof(float));
			inst.sound->buffer.cursor += frames_read;
		} break;
		}
		if (frames_read <= 1) break;

		// Read the data into the buffer provided by ISAC
		memcpy(&output[total_frames_read * AU_CHANNEL_COUNT], au_mix_temp, frames_read * AU_CHANNEL_COUNT * sizeof(float));

		total_frames_read += frames_read;
		if (frames_read < frames_to_read) {
			break;  // Reached EOF.
		}
	}

	return total_frames_read;
}

///////////////////////////////////////////

void isac_data_callback(float** sourceBuffers, uint32_t numSources, uint32_t numFrames, vec3* positions, float* volumes) {
	// Assert on debug builds, eliminate warning on release builds
	//UNREFERENCED_PARAMETER(numSources);
	assert(numSources == _countof(au_active_sounds));

	for (uint32_t i = 0; i < _countof(au_active_sounds); i++) {
		if (au_active_sounds[i].sound == nullptr)
			continue;

		ma_uint32 framesRead = readDataForIsac(au_active_sounds[i], sourceBuffers[i], numFrames, &positions[i], &volumes[i]);
		if (framesRead < numFrames && au_active_sounds[i].sound->type != sound_type_stream) {
			sound_release(au_active_sounds[i].sound);
			au_active_sounds[i].sound = nullptr;
		}
	}
}

///////////////////////////////////////////

int32_t mic_device_count() {
	ma_uint32 capture_count = 0;
	if (ma_context_get_devices(&au_context, nullptr, nullptr, nullptr, &capture_count) != MA_SUCCESS) {
		return 0;
	}
	return capture_count;
}

///////////////////////////////////////////

const char *mic_device_name(int32_t index) {
	ma_device_info *capture_devices = nullptr;
	ma_uint32       capture_count   = 0;
	if (ma_context_get_devices(&au_context, nullptr, nullptr, &capture_devices, &capture_count) != MA_SUCCESS) {
		return nullptr;
	}
	if (index >= 0 && index < (int32_t)capture_count)
		return capture_devices[index].name;
	return nullptr;
}

///////////////////////////////////////////

void mic_callback(ma_device* device, void* output, const void* input, ma_uint32 frame_count) {
	if (input == nullptr || au_mic_sound == nullptr) return;

	sound_write_samples(au_mic_sound, (float*)input, frame_count);
}

///////////////////////////////////////////

sound_t mic_start(const char *device_name) {
	// Make sure we're not starting up an already recording mic
	if (au_recording) {
		if (device_name == nullptr) {
			if (au_mic_name == nullptr)
				return au_mic_sound;
		} else if (au_mic_name != nullptr && strcmp(device_name, au_mic_name) == 0) {
			return au_mic_sound;
		}
		mic_stop();
	}
	au_mic_name = device_name == nullptr 
		? nullptr
		: string_copy(device_name);

	// Find the id of the mic based on the given name
	ma_device_id *id = nullptr;
	if (device_name != nullptr) {
		ma_device_info *capture_devices = nullptr;
		ma_uint32       capture_count   = 0;
		if (ma_context_get_devices(&au_context, nullptr, nullptr, &capture_devices, &capture_count) != MA_SUCCESS) {
			return nullptr;
		}
		for (ma_uint32 i = 0; i < capture_count; i++) {
			if (strcmp(capture_devices[i].name, au_mic_name) == 0) {
				id = &capture_devices[i].id;
				break;
			}
		}
	}

	// Start up the mic
	ma_device_config config   = ma_device_config_init(ma_device_type_capture);
	config.capture.pDeviceID  = id;
	config.capture.format     = AU_SAMPLE_FORMAT;
	config.capture.channels   = AU_CHANNEL_COUNT;
	config.sampleRate         = AU_SAMPLE_RATE;
	config.dataCallback       = mic_callback;
	config.pUserData          = nullptr;
	ma_result result = ma_device_init(&au_context, &config, &au_mic_device);
	if (result != MA_SUCCESS) {
		log_warnf("mic_start has failed: %d", result);
		return nullptr;
	}
	ma_device_start(&au_mic_device);

	// And make sure we have a streaming sound to store mic data in
	if (au_mic_sound == nullptr) {
		au_mic_sound = sound_create_stream(0.5f);
		sound_set_id(au_mic_sound, "sk/mic_sound");
	}

	au_recording = true;
	return au_mic_sound;
}

///////////////////////////////////////////

void mic_stop() {
	free(au_mic_name);
	au_mic_name = nullptr;
	ma_device_stop  (&au_mic_device);
	ma_device_uninit(&au_mic_device);
	memset(&au_mic_device, 0, sizeof(au_mic_device));
	au_recording = false;
}

///////////////////////////////////////////

bool audio_init() {

	if (ma_context_init(nullptr, 0, nullptr, &au_context) != MA_SUCCESS) {
		return false;
	}

#if defined(SK_OS_WINDOWS) || defined(SK_OS_WINDOWS_UWP)
	isac_adapter = new IsacAdapter(_countof(au_active_sounds));
	HRESULT hr = isac_adapter->Activate(isac_data_callback);

	if (SUCCEEDED(hr)) {
		log_diag("Using audio backend: ISAC");
		return true;
	} else if (hr == E_NOT_VALID_STATE){
		log_diag("ISAC not available, falling back to miniaudio! It's likely the device doesn't have Windows Sonic enabled, which can be found under Settings->Sound->Device Properties->Spatial Sound.");
	} else {
		log_warnf("ISAC failed 0x%X, falling back to miniaudio!", hr);
	}
	delete isac_adapter;
	isac_adapter = nullptr;
#endif

	au_config = ma_device_config_init(ma_device_type_playback);
	au_config.playback.format   = AU_SAMPLE_FORMAT;
	au_config.playback.channels = AU_CHANNEL_COUNT;
	au_config.sampleRate        = AU_SAMPLE_RATE;
	au_config.dataCallback      = data_callback;
	au_config.pUserData         = nullptr;

	if (ma_device_init(&au_context, &au_config, &au_device) != MA_SUCCESS) {
		log_err("miniaudio: Failed to open playback device.");
		return false;
	}

	if (ma_device_start(&au_device) != MA_SUCCESS) {
		log_err("miniaudio: Failed to start playback device.");
		ma_device_uninit(&au_device);
		return false;
	}

	au_mic_name = nullptr;

	log_diagf("miniaudio: using backend %s", ma_get_backend_name(au_device.pContext->backend));
	return true;
}

///////////////////////////////////////////

void audio_update() {
	matrix head = pose_matrix(*input_head());
	matrix_inverse(head, au_head_transform);
}

///////////////////////////////////////////

void audio_shutdown() {
	mic_stop();
#if defined(SK_OS_WINDOWS) || defined(SK_OS_WINDOWS_UWP)
	if (isac_adapter)
		delete isac_adapter;
#endif
	ma_device_uninit (&au_device);
	ma_context_uninit(&au_context);
	sound_release(au_mic_sound);
}

}