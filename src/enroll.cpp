#include "fingerprint.h"

using namespace v8;
using v8::FunctionTemplate;

typedef struct __ENROLL_DATA__ {
    uv_async_t async;
    Nan::Persistent<Function> callback;
    unsigned char *fingerprint_data;
    size_t fingerprint_size;
    int result;
} ENROLL_DATA;

typedef struct __ENROLL_STOP__ {
    uv_async_t async;
    Nan::Persistent<Function> callback;
} ENROLL_STOP;

static const char *enroll_result_to_name(int result)
{
	switch (result) {
	case FP_ENROLL_COMPLETE: return "enroll-completed";
	case FP_ENROLL_FAIL: return "enroll-failed";
	case FP_ENROLL_PASS: return "enroll-stage-passed";
	case FP_ENROLL_RETRY: return "enroll-retry-scan";
	case FP_ENROLL_RETRY_TOO_SHORT: return "enroll-swipe-too-short";
	case FP_ENROLL_RETRY_CENTER_FINGER: return "enroll-finger-not-centered";
	case FP_ENROLL_RETRY_REMOVE_FINGER: return "enroll-remove-and-retry";
	default: return "enroll-unknown-error";
	}
}

void enroll_stopped_after(uv_handle_t* handle)
{
    ENROLL_STOP *data = container_of((uv_async_t *)handle, ENROLL_STOP, async);

    if(!data)
        return;

    delete data;
}

#ifndef OLD_UV_RUN_SIGNATURE
void report_enroll_stopped(uv_async_t *handle)
#else
void report_enroll_stopped(uv_async_t *handle, int status)
#endif
{
    ENROLL_STOP *data = container_of(handle, ENROLL_STOP, async);
    Nan::HandleScope();

    if(!data)
        return;

    Nan::Callback callback(Nan::New<Function>(data->callback));
    callback.Call(0, NULL);

    uv_close((uv_handle_t*)&data->async, enroll_stopped_after);
}

static void enroll_stop_cb(struct fp_dev *dev, void *user_data)
{
    ENROLL_STOP *data = (ENROLL_STOP*)user_data;

    if(!data)
        return;

    uv_async_send(&data->async);
}

NAN_METHOD(enrollStop) {
    struct fp_dev *dev;
    bool ret = false;
    ENROLL_STOP *data;

    if(info.Length() < 2)
        return;

    dev = toFPDev(info[0]->ToNumber()->Value());
    if(initalized != 0 || dev == NULL)
        goto error;

    data = new ENROLL_STOP;
    data->callback.Reset(v8::Local<v8::Function>::Cast(info[1]));
    uv_async_init(uv_default_loop(), &data->async, report_enroll_stopped);
    ret = fp_async_enroll_stop(dev, enroll_stop_cb, data) == 0;

error:
    info.GetReturnValue().Set(Nan::New(ret));
    return;
}

void enroll_after(uv_handle_t* handle)
{
    ENROLL_DATA *enrollData = container_of((uv_async_t *)handle, ENROLL_DATA, async);

    if(!enrollData)
        return;

    if(enrollData->fingerprint_data)
        free(enrollData->fingerprint_data);

    enrollData->fingerprint_data = NULL;
    enrollData->fingerprint_size = 0;
    delete enrollData;
}

#ifndef OLD_UV_RUN_SIGNATURE
void report_enroll_progress(uv_async_t *handle)
#else
void report_enroll_progress(uv_async_t *handle, int status)
#endif
{
    ENROLL_DATA *enrollData = container_of(handle, ENROLL_DATA, async);
    Nan::HandleScope();

    if(!enrollData)
        return;

    Nan::Callback callback(Nan::New<Function>(enrollData->callback));
    Local<Value> argv[3];
    argv[0] = Nan::New(enrollData->result);
    argv[1] = Nan::Null();
    argv[2] = Nan::Null();

    if(enroll_result_to_name(enrollData->result))
        argv[1] = Nan::New(enroll_result_to_name(enrollData->result)).ToLocalChecked();

    if(enrollData->result == FP_ENROLL_COMPLETE) {
        std::string fingerprint = toString(enrollData->fingerprint_data, enrollData->fingerprint_size);
        if(fingerprint.empty()) {
            Nan::ThrowError("fingerprint data convert failed!");
            return;
        }
        argv[2] = Nan::New(fingerprint.c_str()).ToLocalChecked();
    }

    callback.Call(3, argv);

    if(enrollData->result == FP_ENROLL_FAIL || enrollData->result == FP_ENROLL_COMPLETE) {
        uv_close((uv_handle_t*)&enrollData->async, enroll_after);
    }
}

static void enroll_stage_cb(struct fp_dev *dev, int result, struct fp_print_data *print, struct fp_img *img, void *user_data)
{
    ENROLL_DATA *enrollData = (ENROLL_DATA*)user_data;

    if(!enrollData || result < 0)
        return;

    enrollData->result = result;
    if(print && result == FP_ENROLL_COMPLETE)
        enrollData->fingerprint_size = fp_print_data_get_data(print, &enrollData->fingerprint_data);

    uv_async_send(&enrollData->async);

    if(img) fp_img_free(img);
    if(print) fp_print_data_free(print);
}

NAN_METHOD(enrollStart) {
    int r;
    struct fp_dev *dev;
    bool ret = false;
    ENROLL_DATA *enrollData;

    if(info.Length() < 2)
        return;

    dev = toFPDev(info[0]->ToNumber()->Value());
    if(initalized != 0 || dev == NULL)
        goto error;

    enrollData = new ENROLL_DATA;
    if(!enrollData)
        goto error;

    enrollData->fingerprint_data = NULL;
    enrollData->fingerprint_size = 0;
    enrollData->result = -1;
    uv_async_init(uv_default_loop(), &enrollData->async, report_enroll_progress);
    enrollData->callback.Reset(v8::Local<v8::Function>::Cast(info[1]));
    r = fp_async_enroll_start(dev, enroll_stage_cb, (void*)enrollData);
    if (r < 0 || r == FP_ENROLL_FAIL) {
        printf("Enroll failed with error %d\n", r);
        goto error;
    }

    ret = true;
error:
    info.GetReturnValue().Set(Nan::New(ret));
    return;
}
