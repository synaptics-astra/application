project('synacompositor', ['c','cpp'],
  version: '1.0',
  default_options: ['warning_level=2']
)

egl_dep = dependency('egl', required: true)
gles2_dep = dependency('glesv2', required: true)
drm_dep = dependency('libdrm', required: true)
gbm_dep = dependency('gbm', required: true)

# List of source files
synacompositor_sources = files('source/EGLManager.cpp', 'source/DRMBuffer.cpp', 'source/BufferObject.cpp', 'source/ShaderProgram.cpp', 'source/Texture.cpp', 'source/SynaCompositor.cpp')
synacompositor_includes = include_directories('include')

synacompositor_lib = shared_library('synacompositor',
  synacompositor_sources,
  include_directories: synacompositor_includes,
  cpp_args: ['-g'],
  link_args: ['-lm'],
  dependencies: [egl_dep, gles2_dep, drm_dep, gbm_dep],
  install: true
)
