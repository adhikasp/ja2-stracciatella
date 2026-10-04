file(GLOB LOCAL_JA2_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
set(LOCAL_JA2_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/UiMode.cc ${CMAKE_CURRENT_SOURCE_DIR}/ViewModelLua.cc)
set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/ViewModelLua.cc DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY SKIP_UNITY_BUILD_INCLUSION ON)

if (WITH_NATIVE_UI)
    set(NATIVE_UI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/NativeUI.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/NativeImages.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/ViewModel.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/GameViewModels.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/CreditsScreen.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/MockScreen.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/MockWorld.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/TacticalHud.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/LaptopImages.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/LaptopScreen.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/LaptopSites.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/LaptopNativeScreen.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/FrontEndSupport.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/FrontSetup.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/FrontMainMenu.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/FrontOptions.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/FrontSaveLoad.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/FrontNewGame.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/MapScreenNative.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/MapScreenModel.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/AutoResolveNative.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/AutoResolveModel.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/ShopKeeperNative.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/ShopKeeperModel.cc
    )
    if (WITH_UNITTESTS)
        list(APPEND NATIVE_UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/NativeUI_unittest.cc ${CMAKE_CURRENT_SOURCE_DIR}/MapScreenModel_unittest.cc ${CMAKE_CURRENT_SOURCE_DIR}/AutoResolveModel_unittest.cc ${CMAKE_CURRENT_SOURCE_DIR}/ShopKeeperModel_unittest.cc)
    endif()
    # RmlUi headers are heavy and its names are generic: keep these out of the unity batches
    set_property(SOURCE ${NATIVE_UI_SOURCES} DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY SKIP_UNITY_BUILD_INCLUSION ON)
    list(APPEND LOCAL_JA2_SOURCES ${NATIVE_UI_SOURCES})
else()
    list(APPEND LOCAL_JA2_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/NativeUIStub.cc)
endif()

set(JA2_SOURCES
    ${JA2_SOURCES}
    ${LOCAL_JA2_HEADERS}
    ${LOCAL_JA2_SOURCES}
    PARENT_SCOPE
)
