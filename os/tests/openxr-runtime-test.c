#include <openxr/openxr.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int report_failure(const char *operation, XrResult result) {
    fprintf(stderr, "STEARLIGHT OPENXR: %s failed (%d)\n", operation,
            (int)result);
    return 1;
}

int main(void) {
    XrInstanceCreateInfo create_info = {
        .type = XR_TYPE_INSTANCE_CREATE_INFO,
        .applicationInfo = {
            .applicationName = "Stearlight OpenXR probe",
            .applicationVersion = 1,
            .engineName = "Stearlight",
            .engineVersion = 1,
            .apiVersion = XR_CURRENT_API_VERSION,
        },
    };
    XrInstance instance = XR_NULL_HANDLE;
    XrResult result = xrCreateInstance(&create_info, &instance);
    if (result != XR_SUCCESS)
        return report_failure("xrCreateInstance", result);

    XrInstanceProperties instance_properties = {
        .type = XR_TYPE_INSTANCE_PROPERTIES,
    };
    result = xrGetInstanceProperties(instance, &instance_properties);
    if (result != XR_SUCCESS) {
        xrDestroyInstance(instance);
        return report_failure("xrGetInstanceProperties", result);
    }

    XrSystemGetInfo system_info = {
        .type = XR_TYPE_SYSTEM_GET_INFO,
        .formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY,
    };
    XrSystemId system = XR_NULL_SYSTEM_ID;
    result = xrGetSystem(instance, &system_info, &system);
    if (result != XR_SUCCESS) {
        xrDestroyInstance(instance);
        return report_failure("xrGetSystem", result);
    }

    XrSystemProperties system_properties = {
        .type = XR_TYPE_SYSTEM_PROPERTIES,
    };
    result = xrGetSystemProperties(instance, system, &system_properties);
    if (result != XR_SUCCESS) {
        xrDestroyInstance(instance);
        return report_failure("xrGetSystemProperties", result);
    }

    uint32_t view_count = 0;
    result = xrEnumerateViewConfigurationViews(
        instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
        &view_count, NULL);
    if (result != XR_SUCCESS || view_count < 2) {
        xrDestroyInstance(instance);
        return report_failure("xrEnumerateViewConfigurationViews", result);
    }

    XrViewConfigurationView *views = calloc(view_count, sizeof(*views));
    if (!views) {
        xrDestroyInstance(instance);
        fprintf(stderr, "STEARLIGHT OPENXR: view allocation failed\n");
        return 1;
    }
    for (uint32_t index = 0; index < view_count; ++index)
        views[index].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    result = xrEnumerateViewConfigurationViews(
        instance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        view_count, &view_count, views);
    if (result != XR_SUCCESS) {
        free(views);
        xrDestroyInstance(instance);
        return report_failure("xrEnumerateViewConfigurationViews(data)",
                              result);
    }
    for (uint32_t index = 0; index < view_count; ++index) {
        if (!views[index].recommendedImageRectWidth ||
            !views[index].recommendedImageRectHeight) {
            free(views);
            xrDestroyInstance(instance);
            fprintf(stderr,
                    "STEARLIGHT OPENXR: view %u has no recommended render size\n",
                    index);
            return 1;
        }
    }

    printf("STEARLIGHT OPENXR READY runtime=%s system=%s views=%u size=%ux%u\n",
           instance_properties.runtimeName, system_properties.systemName,
           view_count, views[0].recommendedImageRectWidth,
           views[0].recommendedImageRectHeight);
    free(views);
    xrDestroyInstance(instance);
    return 0;
}
