from distutils.core import setup, Extension
import sys

ht_home = '/opt/hypertable/0.9.8.11'
include_dirs = [ht_home+'/include']
if 'PyPy' in sys.version:
    include_dirs.append('/opt/pypy2/include/')

extra_objects = []
for l in [
    'libHypertable.a',
    'libHyperThirdParty.a',
    'libHyperThrift.a',
    'libHyperThriftConfig.a',
    'libHyperTools.a',
    'libHyperAppHelper.a',
    'libHyperComm.a',
    'libHyperCommon.a',
    'libHyperFsBroker.a',
    'libHyperMaster.a',
    'libHyperRanger.a',
    'libHyperspace.a'
]:
    extra_objects.append(ht_home+'/lib/'+l)

ht = Extension('libPyHypertable',
               sources=['src/pyhypertable.cc'],
               include_dirs=include_dirs,
               libraries=['expat', 'sigar-amd64-linux', 'dl', 'boost_iostreams', 'boost_program_options',
                          'boost_filesystem', 'boost_thread', 'boost_system', 'boost_chrono', 'edit', 'ncursesw',
                          'snappy', 'pthread', 're2', 'tcmalloc_minimal', 'unwind', 'ssl', 'crypto'],
               extra_objects=extra_objects,
               library_dirs=[ht_home+'/lib'],
               extra_compile_args=["-std=c++17", "-O3", "-flto", "-fuse-linker-plugin", "-ffat-lto-objects"],
               # language='c++17',
               runtime_library_dirs=['/usr/local/lib'],
               )

setup(name='PyHypertable',
      version='0.1',
      author='Kashirin Alex',
      author_email='kashirin.alex@gmail.com',
      description="Python wrapper for Hypertable",
      long_description="""PyHypertable is a Python extension module which wraps Hypertable""",
      url="http://git.com/kashirin-alex/hypertable/src/py/pyhypertable",
      license="Apache License 2.0",
      platforms=["GNU/Linux"],
      ext_modules=[ht])
