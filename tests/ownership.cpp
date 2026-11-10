#include "4DPlugin-JSON.h"
#include "C_TEXT.h"
#include "ARRAY_TEXT.h"
#include "PrivateTypes.h"
#include "EntryPoints.h"
#include "4DPlugin-STOMP.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

uint32_t stomp_context_add(stomp_connection*, apr_pool_t*);

namespace {
using Text = std::basic_string<PA_Unichar>;
std::map<PA_Unichar*, size_t> strings;
std::map<PA_Handle, size_t> handles;
bool nullEmpty = false;
bool failPropertyRead = false;
bool stringifyMismatch = false;
std::string stringifyResult = "{\"header\":\"A\\u65e5\"}";
std::map<std::string, std::string> expectedHeaders;
apr_status_t writeStatus = APR_SUCCESS;
size_t writes = 0;

struct Object {
    std::map<Text, PA_Variable> properties;
    size_t refs = 1;
};
struct Collection {
    size_t refs = 1;
};
std::set<Object*> objects;
std::set<Collection*> collections;

Text text(const PA_Unistring& value) {
    PA_Unichar* buffer = value.fString;
    if (!buffer) {
        assert(value.fLength == 0);
        return Text();
    }
    assert(strings.count(buffer));
    assert(strings.at(buffer) == static_cast<size_t>(value.fLength));
    assert(buffer[value.fLength] == 0);
    return Text(buffer, value.fLength);
}

PA_Unistring makeString(const Text& value) {
    PA_Unistring result = {};
    if (value.empty() && nullEmpty) return result;
    PA_Unichar* buffer = new PA_Unichar[value.size() + 1];
    std::copy(value.begin(), value.end(), buffer);
    buffer[value.size()] = 0;
    result.fString = buffer;
    result.fLength = static_cast<PA_long32>(value.size());
    assert(strings.emplace(buffer, value.size()).second);
    return result;
}

void disposeString(PA_Unistring& value) {
    text(value);
    PA_Unichar* buffer = value.fString;
    if (buffer) {
        assert(strings.erase(buffer) == 1);
        delete[] buffer;
    }
    value = {};
}

Object* newObject() {
    Object* obj = new Object;
    objects.insert(obj);
    return obj;
}

Collection* newCollection() {
    Collection* collection = new Collection;
    collections.insert(collection);
    return collection;
}

PA_Variable copyVariable(const PA_Variable& input) {
    PA_Variable output = input;
    switch (input.fType) {
        case eVK_Unistring:
            output.uValue.fString = makeString(text(input.uValue.fString));
            break;
        case eVK_Object:
            if (input.uValue.fObject) {
                Object* obj = static_cast<Object*>(input.uValue.fObject);
                assert(objects.count(obj));
                ++obj->refs;
            }
            break;
        case eVK_Collection:
            if (input.uValue.fCollection) {
                Collection* collection = static_cast<Collection*>(input.uValue.fCollection);
                assert(collections.count(collection));
                ++collection->refs;
            }
            break;
        default:
            break;
    }
    return output;
}

void clearVariable(PA_Variable& value) {
    switch (value.fType) {
        case eVK_Unistring:
            disposeString(value.uValue.fString);
            break;
        case eVK_Object:
            if (value.uValue.fObject) {
                Object* obj = static_cast<Object*>(value.uValue.fObject);
                assert(objects.count(obj));
                if (--obj->refs == 0) {
                    for (auto& entry : obj->properties) clearVariable(entry.second);
                    objects.erase(obj);
                    delete obj;
                }
            }
            break;
        case eVK_Collection:
            if (value.uValue.fCollection) {
                Collection* collection = static_cast<Collection*>(value.uValue.fCollection);
                assert(collections.count(collection));
                if (--collection->refs == 0) {
                    collections.erase(collection);
                    delete collection;
                }
            }
            break;
        case eVK_ArrayUnicode: {
            PA_Handle handle = value.uValue.fArray.fData;
            if (handle) {
                assert(handles.count(handle));
                assert(handles.at(handle) ==
                       (value.uValue.fArray.fNbElements + 1) * sizeof(PA_Unistring));
                PA_Unistring* data = reinterpret_cast<PA_Unistring*>(*handle);
                for (PA_long32 i = 0; i <= value.uValue.fArray.fNbElements; ++i)
                    disposeString(data[i]);
                std::free(*handle);
                handles.erase(handle);
                delete handle;
            }
            break;
        }
        case eVK_Undefined:
        case eVK_Boolean:
        case eVK_Longint:
        case eVK_Real:
        case eVK_Null:
            break;
        default:
            assert(false && "unexpected variable cleanup");
    }
    value = {};
    value.fType = eVK_Undefined;
}

Text utf16(const char* utf8) {
    CFStringRef str = CFStringCreateWithCString(nullptr, utf8, kCFStringEncodingUTF8);
    assert(str);
    Text result(CFStringGetLength(str), 0);
    if (!result.empty())
        CFStringGetCharacters(str, CFRangeMake(0, result.size()), &result[0]);
    CFRelease(str);
    return result;
}

void engine(short selector, EngineBlock* eb) {
    switch (selector) {
        case EX_CREATE_UNISTRING:
            eb->fUniString1 = makeString(Text(reinterpret_cast<PA_Unichar*>(eb->fHandle)));
            break;
        case EX_DISPOSE_UNISTRING:
            disposeString(eb->fUniString1);
            break;
        case EX_SET_UNISTRING: {
            Text content(reinterpret_cast<PA_Unichar*>(eb->fHandle));
            disposeString(eb->fUniString1);
            eb->fUniString1 = makeString(content);
            break;
        }
        case EX_VARIABLE_TO_STRING: {
            PA_Variable* var = reinterpret_cast<PA_Variable*>(eb->fHandle);
            assert(var->fType == eVK_Unistring);
            text(var->uValue.fString);
            eb->fUniString1 = var->uValue.fString;
            break;
        }
        case EX_CLEAR_VARIABLE:
            clearVariable(*static_cast<PA_Variable*>(eb->fPtr1));
            break;
        case EX_HANDLE_MANAGER: {
            PA_Handle handle = eb->fParam1 == 1 ? nullptr : eb->fHandle;
            sLONG_PTR size = eb->fParam1 == 1 || eb->fParam1 == 3 ? eb->fParam2 : 0;
            switch (eb->fParam1) {
                case 1:
                    handle = new char*;
                    *handle = static_cast<char*>(std::calloc(1, std::max<sLONG_PTR>(1, size)));
                    assert(*handle);
                    handles[handle] = eb->fParam2;
                    eb->fHandle = handle;
                    break;
                case 2:
                    assert(handles.erase(handle) == 1);
                    std::free(*handle);
                    delete handle;
                    break;
                case 3: {
                    assert(handles.count(handle));
                    void* resized = std::realloc(*handle, std::max<sLONG_PTR>(1, size));
                    assert(resized);
                    *handle = static_cast<char*>(resized);
                    handles[handle] = eb->fParam2;
                    break;
                }
                case 4:
                    eb->fParam2 = handles.at(handle);
                    break;
                case 5:
                    assert(handles.count(handle));
                    eb->fParam3 = reinterpret_cast<sLONG_PTR>(*handle);
                    break;
                case 6:
                    assert(handles.count(handle));
                    break;
                default:
                    assert(false && "unexpected handle operation");
            }
            break;
        }
        case EX_SET_OBJ_VALUE: {
            Object* obj = static_cast<Object*>(eb->fPtr1);
            assert(objects.count(obj) && eb->fShort == eVK_Object);
            PA_Variable copied = copyVariable(*static_cast<PA_Variable*>(eb->fPtr2));
            Text key = text(eb->fUniString1);
            auto old = obj->properties.find(key);
            if (old != obj->properties.end()) clearVariable(old->second);
            obj->properties[key] = copied;
            break;
        }
        case EX_GET_OBJ_VALUE: {
            assert(eb->fShort == eVK_Object);
            PA_Variable* output = static_cast<PA_Variable*>(eb->fPtr2);
            if (failPropertyRead) {
                // Deliberately invalid, non-owning output: no failure ownership contract.
                std::memset(output, 0xA5, sizeof(*output));
                eb->fError = eER_BadEventCall;
                break;
            }
            Object* obj = static_cast<Object*>(eb->fPtr1);
            assert(objects.count(obj));
            *output = copyVariable(obj->properties.at(text(eb->fUniString1)));
            break;
        }
        case EX_EXECUTE_COMMAND_BY_ID: {
            PA_Variable** args = static_cast<PA_Variable**>(eb->fPtr1);
            PA_Variable* output = static_cast<PA_Variable*>(eb->fPtr2);
            Object* obj = static_cast<Object*>(args[0]->uValue.fObject);
            assert(objects.count(obj));
            if (eb->fParam1 == 1231) {
                PA_SetBooleanVariable(output,
                    obj->properties.count(text(args[1]->uValue.fString)) != 0);
            } else if (eb->fParam1 == 1225) {
                Object* duplicate = newObject();
                for (const auto& entry : obj->properties)
                    duplicate->properties[entry.first] = copyVariable(entry.second);
                PA_SetObjectVariable(output, duplicate);
            } else if (eb->fParam1 == 1217) {
                if (stringifyMismatch) {
                    PA_SetCollectionVariable(output, newCollection());
                } else {
                    PA_Unistring result = makeString(utf16(stringifyResult.c_str()));
                    PA_SetStringVariable(output, &result);
                }
            } else {
                assert(false && "unexpected command");
            }
            break;
        }
        default:
            std::cerr << "Unexpected engine selector " << selector << std::endl;
            std::abort();
    }
}

void balanced(const char* scenario) {
    if (!strings.empty() || !handles.empty() || !objects.empty() || !collections.empty()) {
        std::cerr << scenario << ": allocation imbalance: strings=" << strings.size()
                  << " handles=" << handles.size() << " objects=" << objects.size()
                  << " collections=" << collections.size() << std::endl;
        std::exit(1);
    }
}

const char* fixtures[] = {"", "header", "\xE6\x97\xA5", "\xF0\x9F\x98\x80"};
const wchar_t* wideFixtures[] = {L"", L"header", L"\u65e5", L"\U0001f600"};

void setters() {
    Object* obj = newObject();
    for (size_t i = 0; i < 4; ++i) {
        Text expected = utf16(fixtures[i]);
        assert(expected.size() == (i == 0 ? 0 : i == 1 ? 6 : i == 2 ? 1 : 2));
        for (int repeat = 0; repeat < 3; ++repeat) {
            ob_set_s(obj, "key", fixtures[i]);
            assert(text(obj->properties.at(utf16("key")).uValue.fString) == expected);
            ob_set_s(obj, L"key", fixtures[i]);
            assert(text(obj->properties.at(utf16("key")).uValue.fString) == expected);
            ob_set_a(obj, L"key", wideFixtures[i]);
            assert(text(obj->properties.at(utf16("key")).uValue.fString) == expected);
            ob_set_a(obj, L"key", &expected);
            assert(text(obj->properties.at(utf16("key")).uValue.fString) == expected);
        }
    }
    PA_DisposeObject(obj);
    balanced("setters");
}

void getters() {
    Object* obj = newObject();
    for (size_t i = 0; i < 4; ++i) {
        obj->properties[utf16("key")].fType = eVK_Unistring;
        obj->properties[utf16("key")].uValue.fString = makeString(utf16(fixtures[i]));
        CUTF8String output;
        for (int repeat = 0; repeat < 3; ++repeat) {
            assert(ob_get_a(obj, L"key", &output));
            assert(output == CUTF8String(reinterpret_cast<const uint8_t*>(fixtures[i])));
            assert(!ob_get_b(obj, L"key"));
            assert(ob_get_n(obj, L"key") == 0.0);
            assert(ob_get_c(obj, L"key") == nullptr);
        }
        clearVariable(obj->properties.at(utf16("key")));
    }
    PA_SetBooleanVariable(&obj->properties[utf16("key")], true);
    assert(ob_get_b(obj, L"key"));
    PA_SetRealVariable(&obj->properties[utf16("key")], 17.25);
    assert(ob_get_n(obj, L"key") == 17.25);
    Collection* collection = newCollection();
    PA_SetCollectionVariable(&obj->properties[utf16("key")], collection);
    PA_CollectionRef returned = ob_get_c(obj, L"key");
    assert(returned == collection && collection->refs == 2);
    CUTF8String sentinel(reinterpret_cast<const uint8_t*>("unchanged"));
    assert(ob_get_a(obj, L"key", &sentinel));
    assert(sentinel == CUTF8String(reinterpret_cast<const uint8_t*>("unchanged")));
    if (collection->refs != 2) {
        std::cerr << "getters: allocation imbalance: strings=" << strings.size()
                  << " collection refs=" << collection->refs << " (expected 2)" << std::endl;
        std::exit(1);
    }
    assert(!ob_get_b(obj, L"key") && collection->refs == 2);
    assert(ob_get_n(obj, L"key") == 0.0 && collection->refs == 2);
    PA_DisposeObject(obj);
    assert(collections.count(collection) && collection->refs == 1);
    PA_DisposeCollection(returned);
    balanced("getters");
}

void rejectedReads() {
    Object* obj = newObject();
    PA_SetBooleanVariable(&obj->properties[utf16("key")], true);
    failPropertyRead = true;
    CUTF8String output;
    assert(ob_get_a(obj, L"key", &output));
    assert(!ob_get_b(obj, L"key"));
    assert(ob_get_n(obj, L"key") == 0.0);
    assert(ob_get_c(obj, L"key") == nullptr);
    failPropertyRead = false;
    assert(!ob_get_a(obj, L"missing", &output));
    assert(!ob_get_b(obj, L"missing"));
    assert(ob_get_n(obj, L"missing") == 0.0);
    assert(ob_get_c(obj, L"missing") == nullptr);
    PA_DisposeObject(obj);
    balanced("rejected reads");
}

void arrays() {
    PA_Variable first = PA_CreateVariable(eVK_Undefined);
    PA_Variable second = PA_CreateVariable(eVK_Undefined);
    BytePtr firstArgs[] = {reinterpret_cast<BytePtr>(&first)};
    BytePtr secondArgs[] = {reinterpret_cast<BytePtr>(&second)};
    ARRAY_TEXT outgoing;
    outgoing.setSize(1);
    for (const char* fixture : fixtures) {
        CUTF8String input(reinterpret_cast<const uint8_t*>(fixture));
        outgoing.appendUTF8String(&input);
    }
    outgoing.toParamAtIndex(firstArgs, 1);
    outgoing.toParamAtIndex(secondArgs, 1);
    for (int i = 1; i <= 4; ++i) {
        PA_Unistring a = PA_GetStringInArray(first, i);
        PA_Unistring b = PA_GetStringInArray(second, i);
        assert(text(a) == utf16(fixtures[i - 1]) && text(a) == text(b));
        assert(!a.fString || a.fString != b.fString);
    }
    ARRAY_TEXT copied;
    copied.fromParamAtIndex(firstArgs, 1);
    PA_ClearVariable(&first);
    for (int i = 1; i <= 4; ++i) {
        CUTF8String output;
        copied.copyUTF8StringAtIndex(&output, i);
        assert(output == CUTF8String(reinterpret_cast<const uint8_t*>(fixtures[i - 1])));
    }
    for (int repeat = 0; repeat < 4; ++repeat) {
        copied.toParamAtIndex(secondArgs, 1);
        for (int i = 1; i <= 4; ++i)
            assert(text(PA_GetStringInArray(second, i)) == utf16(fixtures[i - 1]));
    }
    PA_ResizeArray(&second, 1);
    assert(text(PA_GetStringInArray(second, 1)).empty());
    PA_ClearVariable(&second);

    PA_Variable wrong = PA_CreateVariable(eVK_Longint);
    BytePtr wrongArgs[] = {reinterpret_cast<BytePtr>(&wrong)};
    outgoing.toParamAtIndex(wrongArgs, 1);
    assert(wrong.fType == eVK_Longint);
    outgoing.toParamAtIndex(wrongArgs, 0);
    PA_Variable arr = PA_CreateVariable(eVK_ArrayUnicode);
    PA_Unistring owned = makeString(utf16("rejected"));
    PA_SetStringInArray(wrong, 0, &owned);
    PA_SetStringInArray(arr, -1, &owned);
    PA_SetStringInArray(arr, 1, &owned);
    PA_Variable noData = {};
    noData.fType = eVK_ArrayUnicode;
    PA_SetStringInArray(noData, 0, &owned);
    PA_SetStringInArray(arr, 0, nullptr);
    assert(text(owned) == utf16("rejected"));
    PA_DisposeUnistring(&owned);
    PA_ClearVariable(&arr);
    balanced("arrays");
}

void scalarsAndStringify() {
    for (const char* fixture : fixtures) {
        PA_Unistring param = makeString(utf16("old value"));
        BytePtr args[] = {reinterpret_cast<BytePtr>(&param)};
        C_TEXT input;
        CUTF8String utf8(reinterpret_cast<const uint8_t*>(fixture));
        input.setUTF8String(&utf8);
        assert(input.getUTF16Length() == utf16(fixture).size());
        input.toParamAtIndex(args, 1);
        assert(text(param) == utf16(fixture));
        C_TEXT copied;
        copied.fromParamAtIndex(args, 1);
        PA_DisposeUnistring(&param);
        CUTF8String output;
        copied.copyUTF8String(&output);
        assert(output == utf8);
    }
    Object* obj = newObject();
    obj->properties[utf16("header")].fType = eVK_Unistring;
    obj->properties[utf16("header")].uValue.fString = makeString(utf16("A\xE6\x97\xA5"));
    CUTF8String output;
    ob_stringify(obj, &output);
    assert(output == CUTF8String(reinterpret_cast<const uint8_t*>("{\"header\":\"A\\u65e5\"}")));
    assert(objects.size() == 1 && obj->refs == 1);
    assert(text(obj->properties.at(utf16("header")).uValue.fString) == utf16("A\xE6\x97\xA5"));
    stringifyMismatch = true;
    ob_stringify(obj, &output);
    assert(collections.empty() && objects.size() == 1);
    stringifyMismatch = false;
    PA_DisposeObject(obj);
    balanced("scalars and stringify");
}

void commandWrite(bool legacy) {
    assert(apr_initialize() == APR_SUCCESS);
    apr_pool_t* pool = nullptr;
    assert(apr_pool_create(&pool, nullptr) == APR_SUCCESS);
    stomp_connection connection = {};
    PA_long32 context = stomp_context_add(&connection, pool);
    PA_long32 timeout = 1;
    PA_Unistring command = makeString(utf16("SEND"));
    PA_Unistring body = makeString(utf16("body\xF0\x9F\x98\x80"));
    sLONG_PTR result = 0;
    PA_Variable names = PA_CreateVariable(eVK_Undefined);
    PA_Variable values = PA_CreateVariable(eVK_Undefined);
    Object* obj = newObject();
    PA_ObjectRef object = obj;
    std::string key1(80, 'a'), key2(90, 'b');
    std::string value1 = std::string(160, 'x') + "\xE6\x97\xA5";
    std::string value2 = std::string(180, 'y') + "\xF0\x9F\x98\x80";
    expectedHeaders = {{key1, value1}, {key2, value2}};
    stringifyResult = "{\"" + key1 + "\":\"" + value1 + "\",\"" +
                      key2 + "\":\"" + value2 + "\",\"ignored\":12}";
    ARRAY_TEXT nameInputs, valueInputs;
    nameInputs.setSize(1);
    valueInputs.setSize(1);
    for (const auto& entry : expectedHeaders) {
        nameInputs.appendUTF8String(reinterpret_cast<const uint8_t*>(entry.first.c_str()),
                                   entry.first.size());
        valueInputs.appendUTF8String(reinterpret_cast<const uint8_t*>(entry.second.c_str()),
                                    entry.second.size());
    }
    // Same name again preserves APR's last-value-wins behavior.
    nameInputs.appendUTF8String(reinterpret_cast<const uint8_t*>(key1.c_str()), key1.size());
    valueInputs.appendUTF8String(reinterpret_cast<const uint8_t*>(value1.c_str()), value1.size());
    BytePtr nameArgs[] = {reinterpret_cast<BytePtr>(&names)};
    BytePtr valueArgs[] = {reinterpret_cast<BytePtr>(&values)};
    nameInputs.toParamAtIndex(nameArgs, 1);
    valueInputs.toParamAtIndex(valueArgs, 1);
    void* args[] = {&context, &command, &body, legacy ? static_cast<void*>(&names) : &object,
                    legacy ? static_cast<void*>(&values) : &timeout, &timeout};
    PluginBlock parameters = {};
    parameters.fParameters = args;
    parameters.fResult = &result;
    writes = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        writeStatus = repeat == 1 ? APR_EGENERAL : APR_SUCCESS;
        PluginMain(legacy ? 3 : 6, &parameters);
        assert(result == -writeStatus);
        assert(writes == static_cast<size_t>(repeat + 1));
    }
    expectedHeaders.clear();
    stringifyResult = "{\"ignored\":12}";
    nameInputs.setSize(1);
    valueInputs.setSize(1);
    nameInputs.toParamAtIndex(nameArgs, 1);
    valueInputs.toParamAtIndex(valueArgs, 1);
    writeStatus = APR_SUCCESS;
    PluginMain(legacy ? 3 : 6, &parameters);
    assert(result == 0 && writes == 4);
    if (legacy) {
        valueInputs.setSize(2);
        valueInputs.toParamAtIndex(valueArgs, 1);
        PluginMain(3, &parameters);
        assert(result == 0 && writes == 5);
    }
    PluginMain(4, &parameters);
    assert(result == APR_SUCCESS);
    PA_ClearVariable(&names);
    PA_ClearVariable(&values);
    PA_DisposeObject(obj);
    PA_DisposeUnistring(&command);
    PA_DisposeUnistring(&body);
    apr_terminate();
    balanced(legacy ? "array write" : "object write");
}
}

extern "C" apr_status_t stomp_connect(stomp_connection**, const char*, int, apr_pool_t*) {
    std::abort();
}

extern "C" apr_status_t stomp_read(stomp_connection*, stomp_frame**, apr_pool_t*) {
    std::abort();
}

extern "C" apr_status_t stomp_disconnect(stomp_connection**) {
    return APR_SUCCESS;
}

extern "C" apr_status_t apr_socket_timeout_set(apr_socket_t*, apr_interval_time_t timeout) {
    assert(timeout == 1000);
    return APR_SUCCESS;
}

extern "C" apr_status_t stomp_write(stomp_connection*, stomp_frame* frame, apr_pool_t*) {
    ++writes;
    assert(std::string(frame->command) == "SEND");
    const std::string body = "body\xF0\x9F\x98\x80";
    assert(frame->body_length == body.size());
    assert(std::string(frame->body, frame->body_length) == body);
    std::map<std::string, std::string> observed;
    if (frame->headers) {
        for (apr_hash_index_t* it = apr_hash_first(nullptr, frame->headers);
             it; it = apr_hash_next(it)) {
            const void* key = nullptr;
            void* value = nullptr;
            apr_hash_this(it, &key, nullptr, &value);
            observed[static_cast<const char*>(key)] = static_cast<const char*>(value);
        }
    }
    assert(observed == expectedHeaders);
    assert(!expectedHeaders.empty() || !frame->headers);
    return writeStatus;
}

int main(int argc, char** argv) {
    assert(argc == 3);
    gCall4D = engine;
    nullEmpty = std::string(argv[2]) == "null";
    const std::string scenario(argv[1]);
    if (scenario == "setters") setters();
    else if (scenario == "getters") getters();
    else if (scenario == "rejected") rejectedReads();
    else if (scenario == "arrays") arrays();
    else if (scenario == "scalars") scalarsAndStringify();
    else if (scenario == "object-write") commandWrite(false);
    else if (scenario == "array-write") commandWrite(true);
    else std::abort();
    std::cout << scenario << " passed (" << argv[2] << " empty strings)" << std::endl;
}
