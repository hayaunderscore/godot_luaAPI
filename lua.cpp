#include "lua.h"
#include "luaCallable.h"

#include <map>

Lua::Lua() {
	// Createing lua state instance
	state = luaL_newstate();
    luaL_openlibs(state);

    // push our custom print function so by default it prints to the GDConsole.
	lua_register(state, "print", luaPrint);

	// saving this object into registry
	lua_pushstring(state, "__Lua");
	lua_pushlightuserdata(state, this);
	lua_rawset(state, LUA_REGISTRYINDEX);

	// Creating basic types metatables and saving them in registry
	createVector2Metatable();   // "mt_Vector2"
	createVector3Metatable();   // "mt_Vector3"
    createColorMetatable();     // "mt_Color"
    createRect2Metatable();     // "mt_Rect2"
    createPlaneMetatable();     // "mt_Plane"
    createObjectMetatable();    // "mt_Object"
    createCallableMetatable();  // "mt_Callable"

	// Exposing basic types constructors
	exposeConstructors();
}

Lua::~Lua() {
    // Destroying lua state instance
    lua_close(state);
}

// Bind C++ functions to GDScript
void Lua::_bind_methods() {
    ClassDB::bind_method(D_METHOD("bind_libs", "Array"),&Lua::bindLibs);
    ClassDB::bind_method(D_METHOD("do_file", "File"), &Lua::doFile);
    ClassDB::bind_method(D_METHOD("do_string", "Code"), &Lua::doString);
    ClassDB::bind_method(D_METHOD("push_variant", "var", "Name"), &Lua::pushGlobalVariant);
    ClassDB::bind_method(D_METHOD("pull_variant", "Name"), &Lua::pullVariant);
    ClassDB::bind_method(D_METHOD("expose_constructor", "Object", "LuaConstructorName"), &Lua::exposeObjectConstructor);
    ClassDB::bind_method(D_METHOD("call_function", "LuaFunctionName", "Args"), &Lua::callFunction);
    ClassDB::bind_method(D_METHOD("function_exists","LuaFunctionName"), &Lua::luaFunctionExists);
}

// Binds lua librares with the lua state
void Lua::bindLibs(Array libs) {
    for (int i = 0; i < libs.size(); i++) {
        String lib = ((String)libs.get(i)).to_lower();
        if (lib=="base") {
            luaL_requiref(state, "", luaopen_base, 1);
   	        lua_pop(state, 1);
            // base will override print, so we take it back. User can still override them selfs
            lua_register(state, "print", luaPrint);
        } else if (lib=="table") {
            luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
            lua_pop(state, 1);
        } else if (lib=="string") {
            luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
            lua_pop(state, 1);
        } else if (lib=="math") {
            luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
            lua_pop(state, 1);
        } else if (lib=="os") {
            luaL_requiref(state, LUA_OSLIBNAME, luaopen_os, 1);
            lua_pop(state, 1);
        } else if (lib=="io") {
            luaL_requiref(state, LUA_IOLIBNAME, luaopen_io, 1);
            lua_pop(state, 1);
        } else if (lib=="coroutine") {
            luaL_requiref(state, LUA_COLIBNAME, luaopen_coroutine, 1);
            lua_pop(state, 1);
        } else if (lib=="debug") {
            luaL_requiref(state, LUA_DBLIBNAME, luaopen_debug, 1);
            lua_pop(state, 1);
        } else if (lib=="package") {
            luaL_requiref(state, LUA_LOADLIBNAME, luaopen_package, 1);
            lua_pop(state, 1);
        } else if (lib=="utf8") {
            luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
            lua_pop(state, 1);
        }
    }
}

// call a Lua function from GDScript
Variant Lua::callFunction(String function_name, Array args) {
    // push the error handler on to the stack
    lua_pushcfunction(state, luaErrorHandler);

    // put global function name on stack
    lua_getglobal(state, function_name.ascii().get_data());

    // push args
    for (int i = 0; i < args.size(); ++i) {
        pushVariant(args[i]);
    }

    // error handlers index is -2
    int ret = lua_pcall(state, args.size(), 1, -2);
    if (ret != LUA_OK) {
        return handleError(ret);
    }
    Variant toReturn = getVariant(1);
    lua_pop(state, 1);
    return toReturn;
}

bool Lua::luaFunctionExists(String function_name) {
    int type = lua_getglobal(state, function_name.ascii().get_data());
    lua_pop(state,1);
    return type == LUA_TFUNCTION;
}

// addFile() calls luaL_loadfille with the absolute file path
LuaError* Lua::doFile(String fileName) {
    // push the error handler onto the stack
    lua_pushcfunction(state, luaErrorHandler);

    Error error;
    Ref<FileAccess> file = FileAccess::open(fileName, FileAccess::READ, &error);
    if (error != Error::OK) {
        return LuaError::newErr(vformat("error '%s' while opening file '%s'", error_names[error], fileName), LuaError::ERR_FILE);
    }

    String path = file->get_path_absolute();
    int ret = luaL_loadfile(state, path.ascii().get_data());
    if (ret != LUA_OK) {
        return handleError(ret);
    }

    LuaError* err = execute(-2);
    // pop the error handler from the stack
    lua_pop(state, 1);
    return err;
}

// Run lua string in a thread if threading is enabled
LuaError* Lua::doString(String code) {
    // push the error handler onto the stack
    lua_pushcfunction(state, luaErrorHandler);
    luaL_loadstring(state, code.ascii().get_data());
    LuaError* err = execute(-2);
    // pop the error handler from the stack
    lua_pop(state, 1);
    return err;
}

// Execute the current lua stack, return error as string if one occures, otherwise return String()
LuaError* Lua::execute(int handlerIndex) {
    int ret = lua_pcall(state, 0, 0, handlerIndex);
    if (ret != LUA_OK) {
        return handleError(ret);
    }
    return LuaError::errNone();
}

// Push a GD Variant to the lua stack and return false if type is not supported (in this case, returns a nil value).
LuaError* Lua::pushVariant(Variant var) const {
    switch (var.get_type())
    {
        case Variant::Type::NIL:
            lua_pushnil(state);
            break;
        case Variant::Type::STRING:
            lua_pushstring(state,(var.operator String()).ascii().get_data());
            break;
        case Variant::Type::INT:
            lua_pushinteger(state, (int64_t)var);
            break;
        case Variant::Type::FLOAT:
            lua_pushnumber(state,var.operator double());
            break;
        case Variant::Type::BOOL:
            lua_pushboolean(state, (bool)var);
            break;
        case Variant::Type::PACKED_BYTE_ARRAY:
        case Variant::Type::PACKED_INT64_ARRAY:
        case Variant::Type::PACKED_INT32_ARRAY:
        case Variant::Type::PACKED_STRING_ARRAY:
        case Variant::Type::PACKED_FLOAT64_ARRAY:
        case Variant::Type::PACKED_FLOAT32_ARRAY:
        case Variant::Type::PACKED_VECTOR2_ARRAY:
        case Variant::Type::PACKED_VECTOR3_ARRAY:
        case Variant::Type::PACKED_COLOR_ARRAY:            
        case Variant::Type::ARRAY: {
            Array array = var.operator Array();
            lua_newtable(state);
            for(int i = 0; i < array.size(); i++) {
                Variant key = i+1;
                Variant value = array[i];
                pushVariant(key);
                pushVariant(value);
                lua_settable(state,-3);
            }
            break;
        }
        case Variant::Type::DICTIONARY:
            lua_newtable(state);
            for(int i = 0; i < ((Dictionary)var).size(); i++) {
                Variant key = ((Dictionary)var).keys()[i];
                Variant value = ((Dictionary)var)[key];
                pushVariant(key);
                pushVariant(value);
                lua_settable(state, -3);
            }
            break;
        case Variant::Type::VECTOR2: {
            void* userdata = (Variant*)lua_newuserdata(state, sizeof(Variant));
            memcpy(userdata, (void*)&var, sizeof(Variant));
            luaL_setmetatable(state, "mt_Vector2");
            break;     
        }     
        case Variant::Type::VECTOR3: {
            void* userdata = (Variant*)lua_newuserdata(state, sizeof(Variant));
            memcpy(userdata, (void*)&var, sizeof(Variant));
            luaL_setmetatable(state, "mt_Vector3");
            break;     
        }
        case Variant::Type::COLOR: {
            void* userdata = (Variant*)lua_newuserdata(state, sizeof(Variant));
            memcpy(userdata, (void*)&var, sizeof(Variant));
            luaL_setmetatable(state, "mt_Color");
            break;
        }
        case Variant::Type::RECT2: {
            void* userdata = (Variant*)lua_newuserdata(state, sizeof(Variant));
            memcpy(userdata, (void*)&var, sizeof(Variant));
            luaL_setmetatable(state, "mt_Rect2");
            break;     
        }
        case Variant::Type::PLANE: {
            void* userdata = (Variant*)lua_newuserdata(state, sizeof(Variant));
            memcpy(userdata, (void*)&var, sizeof(Variant));
            luaL_setmetatable(state, "mt_Plane");
            break;     
        }
        case Variant::Type::OBJECT: {
            // If the type being pushed is a lua error, Raise a error
            if (LuaError* err = Object::cast_to<LuaError>(var.operator Object*()); err != nullptr) {
                lua_pushstring(state, err->getMsg().ascii().get_data());
                lua_error(state);
                break;
            }

            void* userdata = (Variant*)lua_newuserdata(state, sizeof(Variant));
            memcpy(userdata, (void*)var, sizeof(Variant));
            luaL_setmetatable(state, "mt_Object");
            break;  
        }
        case Variant::Type::CALLABLE: {
            // If the callable type is a luaCallable, just push the actual lua function onto the stack.
            Callable callable = var.operator Callable();
            if (callable.is_custom()) {
                CallableCustom* custom = callable.get_custom();
                LuaCallable* luaCallable = dynamic_cast<LuaCallable*>(custom);
                if (luaCallable != nullptr) {
                    lua_rawgeti(state, LUA_REGISTRYINDEX, luaCallable->getFuncRef());
                    break;
                }
            }

            void* userdata = (Variant*)lua_newuserdata(state, sizeof(Variant));
            memcpy(userdata, (void*)&var, sizeof(Variant));
            luaL_setmetatable(state, "mt_Callable");
            break;  
        }
        default:
            lua_pushnil(state);
            return LuaError::newErr(vformat("can't pass Variants of type \"%s\" to Lua.", Variant::get_type_name(var.get_type())), LuaError::ERR_TYPE);
    }
    return LuaError::errNone();
}

// Call pushVariant() and set it to a global name
LuaError* Lua::pushGlobalVariant(Variant var, String name) {
    LuaError* err = pushVariant(var);
    if (*err == LuaError::ERR_NONE) {
        lua_setglobal(state, name.ascii().get_data());
        return err;
    }
    return err;
}

// Pull a global variant from Lua to GDScript
Variant Lua::pullVariant(String name) {
    lua_getglobal(state, name.ascii().get_data());
    Variant val = getVariant(1);
    lua_pop(state, 1);
    return val;
}

// get a value at the given index and return as a variant
Variant Lua::getVariant(int index) const {
    Variant result;
    int type = lua_type(state, index);
    switch (type) {
        case LUA_TSTRING:
            result = lua_tostring(state, index);
            break;
        case LUA_TNUMBER:
            result = lua_tonumber(state, index);
            break;
        case LUA_TBOOLEAN:
            result = (bool)lua_toboolean(state, index);
            break;
        case LUA_TUSERDATA:
            result = *(Variant*)lua_touserdata(state, index);
            break;
        case LUA_TTABLE:
        {
            Dictionary dict;
            lua_pushnil(state);  /* first key */
            while (lua_next(state, (index<0)?(index-1):(index)) != 0) {
                Variant key = getVariant(-2);
                Variant value = getVariant(-1);
                dict[key] = value;
                lua_pop(state, 1);
            }
            result = dict;
            break;
        }
        case LUA_TFUNCTION:
        {
            // Put function on the top of the stack and get a ref to it. This will create a copy of the function.
            lua_pushvalue(state, index);
            LuaCallable *callable = memnew(LuaCallable(Ref<Lua>(this), luaL_ref(state, LUA_REGISTRYINDEX), state));
            result = Callable(callable);
            break;
        }
        case LUA_TNIL:{
            break;
        }
        default:
            result = LuaError::newErr(vformat("unkown lua type '%d' in Lua::getVariant", type), LuaError::ERR_RUNTIME);
    }
    return result;
}

int Lua::luaErrorHandler(lua_State* state) {
    const char * msg = lua_tostring(state, -1);
    luaL_traceback(state, state, msg, 2);
    lua_remove(state, -2);
    return 1;
}

// Assumes there is a error in the top of the stack. Pops it.
LuaError* Lua::handleError(int lua_error) const {
    String msg;
    switch(lua_error) {
        case LUA_ERRRUN: {
            msg += "[LUA_ERRRUN - runtime error ]\n";
            msg += lua_tostring(state, -1);
            msg += "\n";
            lua_pop(state, 1);
            break;
        }
        case LUA_ERRSYNTAX:{
            msg += "[LUA_ERRSYNTAX - syntax error ]\n";
            msg += lua_tostring(state, -1);
            msg += "\n";
            lua_pop(state, 1);
            break;
        }
        case LUA_ERRMEM:{
            msg += "[LUA_ERRMEM - memory allocation error ]\n";
            break;
        }
        case LUA_ERRERR:{
            msg += "[LUA_ERRERR - error while handling another error ]\n";
            break;
        }
        default: break;
    }
    
    return LuaError::newErr(msg, static_cast<LuaError::ErrorType>(lua_error));
}

// Lua functions
// Change lua's print function to print to the Godot console by default
int Lua::luaPrint(lua_State* state)
{
    int args = lua_gettop(state);
    String final_string;
    for (int n=1; n<=args; ++n) {
		String it_string;
		
		switch(lua_type(state, n)) {
			case LUA_TUSERDATA:{
				Variant var = *(Variant*) lua_touserdata(state, n);
				it_string = var.operator String();
				break;
			}
			default:{
				it_string = lua_tostring(state, n);
				break;
			}
		}

		final_string += it_string;
		if (n < args) final_string += ", ";
    }

	print_line(final_string);

    return 0;
}

// -----------meta tables-----------------

// These 2 macros helps us in constructing general metamethods.
// We can use "lua" as a "Lua" pointer and arg1, arg2, ..., arg5 as Variants objects
// Check examples in createVector2Metatable
#define LUA_LAMBDA_TEMPLATE(_f_)                          \
 [](lua_State* inner_state) -> int {                      \
     lua_pushstring(inner_state,"__Lua");                 \
     lua_rawget(inner_state,LUA_REGISTRYINDEX);           \
     Lua* lua = (Lua*) lua_touserdata(inner_state,-1);    \
     lua_pop(inner_state, 1);                             \
     Variant arg1 = lua->getVariant(1);                   \
     Variant arg2 = lua->getVariant(2);                   \
     Variant arg3 = lua->getVariant(3);                   \
     Variant arg4 = lua->getVariant(4);                   \
     Variant arg5 = lua->getVariant(5);                   \
     _f_                                                  \
}
 
#define LUA_METAMETHOD_TEMPLATE(lua_state, metatable_index, metamethod_name, _f_)\
lua_pushstring(lua_state, metamethod_name); \
lua_pushcfunction(lua_state, LUA_LAMBDA_TEMPLATE(_f_)); \
lua_settable(lua_state, metatable_index-2);

// Used to keep track of the original pointer via the userdata pointer
static std::map<void*, Variant*> luaObjects;

// Expose the contructor for a object to lua
LuaError* Lua::exposeObjectConstructor(Object* obj, String name) {
    // Make sure we are able to call new
    if (!obj->has_method("new")) {
        return LuaError::newErr("during \"Lua::exposeObjectConstructor\" method 'new' does not exist.", LuaError::ERR_RUNTIME);
    }
    lua_pushlightuserdata(state, obj);
    lua_pushcclosure(state, LUA_LAMBDA_TEMPLATE({
        Object* inner_obj = (Object*)lua_touserdata(inner_state, lua_upvalueindex(1));
        
        // We cant store the variant directly in the userdata. It will causes crashes.
        Variant* var = memnew(Variant);
        *var = inner_obj->call("new");
        void* userdata = (Variant*)lua_newuserdata(inner_state, sizeof(Variant));
        luaObjects[userdata] = var;
        memcpy(userdata, (void*)var, sizeof(Variant));

        luaL_setmetatable(inner_state, "mt_Object");
        return 1;
    }), 1);
    lua_setglobal(state, name.ascii().get_data());
    return LuaError::errNone();
}

// Expose the default constructors
void Lua::exposeConstructors() {
    
    lua_pushcfunction(state,LUA_LAMBDA_TEMPLATE({
        int argc = lua_gettop(inner_state);
        if (argc == 0) {
            lua->pushVariant(Vector2());
        } else {
            lua->pushVariant(Vector2(arg1.operator double(), arg2.operator double()));
        }
        return 1;
    }));
    lua_setglobal(state, "Vector2");
 
    lua_pushcfunction(state,LUA_LAMBDA_TEMPLATE({
        int argc = lua_gettop(inner_state);
        if (argc == 0) {
            lua->pushVariant(Vector3());
        } else {
            lua->pushVariant(Vector3(arg1.operator double(), arg2.operator double(), arg3.operator double()));
        }
        return 1;
    }));
    lua_setglobal(state, "Vector3");
 
    lua_pushcfunction(state,LUA_LAMBDA_TEMPLATE({
        int argc = lua_gettop(inner_state);
        if (argc == 3) {
            lua->pushVariant(Color(arg1.operator double(), arg2.operator double(), arg3.operator double()));
        } else if (argc == 4) {
            lua->pushVariant(Color(arg1.operator double(), arg2.operator double(), arg3.operator double(), arg4.operator double()));
        } else {
            lua->pushVariant(Color());
        }
        return 1;
    }));
    lua_setglobal(state, "Color");
 
    lua_pushcfunction(state,LUA_LAMBDA_TEMPLATE({
        int argc = lua_gettop(inner_state);
        if (argc == 2) {
            lua->pushVariant(Rect2(arg1.operator Vector2(), arg2.operator Vector2()));
        } else if (argc == 4) {
            lua->pushVariant(Rect2(arg1.operator double(), arg2.operator double(), arg3.operator double(), arg4.operator double()));
        } else {
            lua->pushVariant(Rect2());
        }
        return 1;
    }));
    lua_setglobal(state, "Rect2");
 
    lua_pushcfunction(state,LUA_LAMBDA_TEMPLATE({
        int argc = lua_gettop(inner_state);
        if (argc == 4) {
            lua->pushVariant(Plane(arg1.operator double(), arg2.operator double(), arg3.operator double(), arg4.operator double()));
        } else if (argc == 3) {
            lua->pushVariant(Plane(arg1.operator Vector3(), arg2.operator Vector3(), arg3.operator Vector3()));
        } else {
            lua->pushVariant(Plane(arg1.operator Vector3(), arg1.operator double()));
        }
        return 1;
    }));
    lua_setglobal(state, "Plane");
    
}

// This function is used whenever a function is called on one of the userdata types below
int Lua::luaUserdataFuncCall(lua_State* L) {
    lua_pushstring(L,"__Lua");
    lua_rawget(L,LUA_REGISTRYINDEX);
    Lua* lua = (Lua*) lua_touserdata(L,-1);
    lua_pop(L,1);
    int argc = lua_gettop(L);

    Variant arg1 = lua->getVariant(1);
    Variant arg2 = lua->getVariant(2);
    Variant arg3 = lua->getVariant(3);
    Variant arg4 = lua->getVariant(4);
    Variant arg5 = lua->getVariant(5);
    Variant* obj  = (Variant*)lua_touserdata(L, lua_upvalueindex(1));
    String fName = lua->getVariant(lua_upvalueindex(2));
    Variant ret;
    // This feels wrong but cant think of a better way atm. Passing the wrong number of args causes a error.
    switch (argc) {
        case 0:{
            ret = obj->call(fName.ascii().get_data());
            break;
        }
        case 1: {
            ret = obj->call(fName.ascii().get_data(), arg1);
            break;
        }
        case 2: {
            ret = obj->call(fName.ascii().get_data(), arg1, arg2);
            break;
        }
        case 3: {
            ret = obj->call(fName.ascii().get_data(), arg1, arg2, arg3);
            break;
        }
        case 4: {
            ret = obj->call(fName.ascii().get_data(), arg1, arg2, arg3, arg4);
            break;
        }
        case 5: {
            ret = obj->call(fName.ascii().get_data(), arg1, arg2, arg3, arg4, arg5);
            break;
        }
    }
    
    lua->pushVariant(ret);
    return 1;
}

// Create metatable for Vector2 and saves it at LUA_REGISTRYINDEX with name "mt_Vector2"
void Lua::createVector2Metatable() {
    luaL_newmetatable(state, "mt_Vector2");

    LUA_METAMETHOD_TEMPLATE(state, -1, "__index", {
        if (arg1.has_method(arg2)) {
            lua_pushlightuserdata(inner_state, lua_touserdata(inner_state, 1));
            lua->pushVariant(arg2);
            lua_pushcclosure(inner_state, luaUserdataFuncCall, 2);
            return 1;
        }
        
        lua->pushVariant(arg1.get(arg2));
        return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__newindex", {
        // We can't use arg1 here because we need to reference the userdata
        ((Variant*)lua_touserdata(inner_state, 1))->set(arg2, arg3);
        return 0;
    }); 
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__add", {
        lua->pushVariant(arg1.operator Vector2() + arg2.operator Vector2());
    return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__sub", {
        lua->pushVariant(arg1.operator Vector2() - arg2.operator Vector2());
    return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__mul", {
        switch(arg2.get_type()) {
            case Variant::Type::VECTOR2:
                lua->pushVariant(arg1.operator Vector2() * arg2.operator Vector2());
                return 1;
            case Variant::Type::INT:
            case Variant::Type::FLOAT:
                lua->pushVariant(arg1.operator Vector2() * arg2.operator double());
                return 1;
            default:
                return 0;
        }
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__div", {
        switch(arg2.get_type()) {
            case Variant::Type::VECTOR2:
                lua->pushVariant(arg1.operator Vector2() / arg2.operator Vector2());
                return 1;
            case Variant::Type::INT:
            case Variant::Type::FLOAT:
                lua->pushVariant(arg1.operator Vector2() / arg2.operator double());
                return 1;
            default:
                return 0;
        }
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__eq", {
        lua->pushVariant(arg1.operator Vector2() == arg2.operator Vector2());
        return 1;
    });

	LUA_METAMETHOD_TEMPLATE(state, -1, "__lt", {
        lua->pushVariant(arg1.operator Vector2() < arg2.operator Vector2());
        return 1;
    });

	LUA_METAMETHOD_TEMPLATE(state, -1, "__le", {
        lua->pushVariant(arg1.operator Vector2() <= arg2.operator Vector2());
        return 1;
    });
 
    lua_pop(state,1); // Stack is now unmodified
}

// Create metatable for Vector3 and saves it at LUA_REGISTRYINDEX with name "mt_Vector3"
void Lua::createVector3Metatable() {
    luaL_newmetatable(state, "mt_Vector3");
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__index", {
        if (arg1.has_method(arg2)) {
            lua_pushlightuserdata(inner_state, lua_touserdata(inner_state,1));
            lua->pushVariant(arg2);
            lua_pushcclosure(inner_state, luaUserdataFuncCall, 2);
            return 1;
        }
        
        lua->pushVariant(arg1.get(arg2)) ;
        return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__newindex", {
        // We can't use arg1 here because we need to reference the userdata
        ((Variant*)lua_touserdata(inner_state,1))->set(arg2, arg3);
        return 0;
    }); 
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__add", {
        lua->pushVariant(arg1.operator Vector3() + arg2.operator Vector3());
    return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__sub", {
        lua->pushVariant(arg1.operator Vector3() - arg2.operator Vector3());
    return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__mul", {
        switch(arg2.get_type()) {
            case Variant::Type::VECTOR3:
                lua->pushVariant(arg1.operator Vector3() * arg2.operator Vector3());
                return 1;
            case Variant::Type::INT:
            case Variant::Type::FLOAT:
                lua->pushVariant(arg1.operator Vector3() * arg2.operator double());
                return 1;
            default:
                return 0;
        }
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__div", {
        switch(arg2.get_type()) {
            case Variant::Type::VECTOR3:
                lua->pushVariant(arg1.operator Vector3() / arg2.operator Vector3());
                return 1;
            case Variant::Type::INT:
            case Variant::Type::FLOAT:
                lua->pushVariant(arg1.operator Vector3() / arg2.operator double());
                return 1;
            default:
                return 0;
        }
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__eq", {
        lua->pushVariant(arg1.operator Vector3() == arg2.operator Vector3());
        return 1;
    });
 
    lua_pop(state,1); // Stack is now unmodified
}

// Create metatable for Rect2 and saves it at LUA_REGISTRYINDEX with name "mt_Rect2"
void Lua::createRect2Metatable() {
    luaL_newmetatable(state, "mt_Rect2");
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__index", {
        // Index was not found, so check to see if there is a matching function
        if (arg1.has_method(arg2)) {
            lua_pushlightuserdata(inner_state, lua_touserdata(inner_state,1));
            lua->pushVariant(arg2);
            lua_pushcclosure(inner_state, luaUserdataFuncCall, 2);
            return 1;
        }
        
        lua->pushVariant(arg1.get(arg2));
        return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__newindex", {
        // We can't use arg1 here because we need to reference the userdata
        ((Variant*)lua_touserdata(inner_state,1))->set(arg2, arg3);
        return 0;
        
    }); 
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__eq", {
        lua->pushVariant(arg1.operator Rect2() == arg2.operator Rect2());
        return 1;
    });
 
    lua_pop(state,1); // Stack is now unmodified
}

// Create metatable for Plane and saves it at LUA_REGISTRYINDEX with name "mt_Plane"
void Lua::createPlaneMetatable() {
    luaL_newmetatable(state, "mt_Plane");
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__index", {
        if (arg1.has_method(arg2)) {
            lua_pushlightuserdata(inner_state, lua_touserdata(inner_state,1));
            lua->pushVariant(arg2);
            lua_pushcclosure(inner_state, luaUserdataFuncCall, 2);
            return 1;
        }

        lua->pushVariant(arg1.get(arg2));
        return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__newindex", {
        // We can't use arg1 here because we need to reference the userdata
        ((Variant*)lua_touserdata(inner_state,1))->set(arg2, arg3);
        return 0;
    }); 
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__eq", {
        lua->pushVariant(arg1.operator Plane() == arg2.operator Plane());
        return 1;
    });
    
    lua_pop(state,1); // Stack is now unmodified
}

// Create metatable for Color and saves it at LUA_REGISTRYINDEX with name "mt_Color"
void Lua::createColorMetatable() {
    luaL_newmetatable(state, "mt_Color");
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__index", {
        if (arg1.has_method(arg2)) {
            lua_pushlightuserdata(inner_state, lua_touserdata(inner_state,1));
            lua->pushVariant(arg2);
            lua_pushcclosure(inner_state, luaUserdataFuncCall, 2);
            return 1;
        }
        
        lua->pushVariant(arg1.get(arg2)) ;
        return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__newindex", {
        // We can't use arg1 here because we need to reference the userdata
        ((Variant*)lua_touserdata(inner_state,1))->set(arg2, arg3);
        return 0;
    }); 
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__add", {
        lua->pushVariant(arg1.operator Color() + arg2.operator Color());
    return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__sub", {
        lua->pushVariant(arg1.operator Color() - arg2.operator Color());
    return 1;
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__mul", {
        switch(arg2.get_type()) {
            case Variant::Type::COLOR:
                lua->pushVariant(arg1.operator Color() * arg2.operator Color());
                return 1;
            case Variant::Type::INT:
            case Variant::Type::FLOAT:
                lua->pushVariant(arg1.operator Color() * arg2.operator double());
                return 1;
            default:
                return 0;
        }
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__div", {
        switch(arg2.get_type()) {
            case Variant::Type::COLOR:
                lua->pushVariant(arg1.operator Color() / arg2.operator Color());
                return 1;
            case Variant::Type::INT:
            case Variant::Type::FLOAT:
                lua->pushVariant(arg1.operator Color() / arg2.operator double());
                return 1;
            default:
                return 0;
        }
    });
 
    LUA_METAMETHOD_TEMPLATE(state, -1, "__eq", {
        lua->pushVariant(arg1.operator Color() == arg2.operator Color());
        return 1;
    });
 
    lua_pop(state,1); // Stack is now unmodified
}

// Create metatable for any Object and saves it at LUA_REGISTRYINDEX with name "mt_Object"
void Lua::createObjectMetatable() {
    luaL_newmetatable(state, "mt_Object");

    LUA_METAMETHOD_TEMPLATE(state, -1, "__index", {
        // If object overrides
        if (arg1.has_method("__index")) {
            lua->pushVariant(arg1.call("__index", arg2));
            return 1;
        }

        Array allowedFuncs = Array();
        if (arg1.has_method("lua_funcs")) {
            allowedFuncs = arg1.call("lua_funcs");
        }
        // If the functions is allowed and exists 
        if ((allowedFuncs.is_empty() || allowedFuncs.has(arg2)) && arg1.has_method(arg2)) {
            lua_pushlightuserdata(inner_state, lua_touserdata(inner_state, 1));
            lua->pushVariant(arg2);
            lua_pushcclosure(inner_state, luaUserdataFuncCall, 2);
            return 1;
        }

        Array allowedFields = Array();
        if (arg1.has_method("lua_fields")) {
            allowedFields = arg1.call("lua_fields");
        }
        // If the field is allowed
        if (allowedFields.is_empty() || allowedFields.has(arg2)) {
            Variant var = arg1.get(arg2);
            lua->pushVariant(var);
            return 1;
        }
        
        return 0;
    });
    
    LUA_METAMETHOD_TEMPLATE(state, -1, "__newindex", {
        // If object overrides
        if (arg1.has_method("__newindex")) {
            lua->pushVariant(arg1.call("__newindex", arg2, arg3));
            return 1;
        }

        Array allowedFields = Array();
        if (arg1.has_method("lua_fields")) {
            allowedFields = arg1.call("lua_fields");
        }
        
        if (allowedFields.is_empty() || allowedFields.has(arg2)) {
            // We can't use arg1 here because we need to reference the userdata
            ((Variant*)lua_touserdata(inner_state, 1))->set(arg2, arg3);
        }
        return 0;
    }); 
    
    // Makeing sure to clean up the pointer
    LUA_METAMETHOD_TEMPLATE(state, -1, "__gc", {
        void* luaPTR = lua_touserdata(inner_state, 1);
        // Indexing by the userdata pointer to get the og pointer for cleanup
        if (luaObjects.count(luaPTR) > 0) {
            Variant* ptr = luaObjects[luaPTR];
            if (ptr != nullptr) memdelete(ptr);
        }
        return 0;
    });

    lua_pop(state,1);
}

int Lua::luaCallableCall(lua_State* state) {
    lua_pushstring(state, "__Lua");
    lua_rawget(state, LUA_REGISTRYINDEX);
    Lua* lua = (Lua*) lua_touserdata(state, -1);
    lua_pop(state, 1);

    int argc = lua_gettop(state)-1; // We subtract 1 becuase the callable its seld will be counted
    Callable callable = (Callable) lua->getVariant(1);

    Variant arg1 = lua->getVariant(2);
    Variant arg2 = lua->getVariant(3);
    Variant arg3 = lua->getVariant(4);
    Variant arg4 = lua->getVariant(5);
    Variant arg5 = lua->getVariant(6);

    const Variant* args[5] = {
        &arg1,
        &arg2,
        &arg3,
        &arg4,
        &arg5,
    };

    Variant returned;
    Callable::CallError error;
    callable.call(args, argc, returned, error);
    if (error.error != error.CALL_OK) {
        // TODO: Better error handling, maybe this should be passed to errorHandler instead since this could be user triggered as well.
        print_error(vformat("Error during \"Lua::luaCallableCall\" on Callable \"%s\": %d", callable, error.error));
        return 0;
    }
    
    lua->pushVariant(returned);
    return 1;
}

// Create metatable for any Callable and saves it at LUA_REGISTRYINDEX with name "mt_Callable"
void Lua::createCallableMetatable() {
    luaL_newmetatable(state, "mt_Callable");

    lua_pushstring(state, "__call"); 
    lua_pushcfunction(state, luaCallableCall);
    lua_settable(state, -3);
    
    lua_pop(state, 1);
}