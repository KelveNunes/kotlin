// CURIOUS_ABOUT writeToParcel, createFromParcel, <clinit>, describeContents
// LOCAL_VARIABLES_TABLE

import kotlinx.android.parcel.*
import android.os.Parcelable

@Parcelize
class User(val firstName: String, val lastName: String, val age: Int, val isProUser: Boolean) : Parcelable