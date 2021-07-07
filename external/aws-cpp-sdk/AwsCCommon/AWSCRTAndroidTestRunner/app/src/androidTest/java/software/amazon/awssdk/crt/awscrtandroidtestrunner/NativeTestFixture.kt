package software.amazon.awssdk.crt.awscrtandroidtestrunner

open class NativeTestFixture {
    companion object {
        // Used to load the 'native-lib' library on application startup.
        init {
            System.loadLibrary("native-lib")
        }
    }

    external fun runTest(name: String): Int
}
