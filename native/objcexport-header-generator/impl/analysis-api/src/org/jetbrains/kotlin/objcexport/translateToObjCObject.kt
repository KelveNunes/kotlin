package org.jetbrains.kotlin.objcexport

import org.jetbrains.kotlin.analysis.api.KtAnalysisSession
import org.jetbrains.kotlin.analysis.api.symbols.KtClassKind
import org.jetbrains.kotlin.analysis.api.symbols.KtClassOrObjectSymbol
import org.jetbrains.kotlin.analysis.api.symbols.markers.KtSymbolWithModality
import org.jetbrains.kotlin.backend.konan.objcexport.*
import org.jetbrains.kotlin.descriptors.Modality
import org.jetbrains.kotlin.objcexport.analysisApiUtils.getAllMembers
import org.jetbrains.kotlin.objcexport.analysisApiUtils.getDefaultSuperClassOrProtocolName
import org.jetbrains.kotlin.objcexport.analysisApiUtils.getSuperClassSymbolNotAny
import org.jetbrains.kotlin.objcexport.analysisApiUtils.isVisibleInObjC

context(KtAnalysisSession, KtObjCExportSession)
fun KtClassOrObjectSymbol.translateToObjCObject(): ObjCClass? {
    require(classKind == KtClassKind.OBJECT)
    if (!isVisibleInObjC()) return null

    val superClass = getSuperClassSymbolNotAny()
    val kotlinAnyName = getDefaultSuperClassOrProtocolName()
    val superName = if (superClass == null) kotlinAnyName else throw RuntimeException("Super class translation isn't implemented yet")
    val enumKind = this.classKind == KtClassKind.ENUM_CLASS
    val final = if (this is KtSymbolWithModality) this.modality == Modality.FINAL else false
    val attributes = if (enumKind || final) listOf(OBJC_SUBCLASSING_RESTRICTED) else emptyList()

    val name = getObjCClassOrProtocolName()
    val comment: ObjCComment? = annotationsList.translateToObjCComment()
    val origin: ObjCExportStubOrigin = getObjCExportStubOrigin()
    val superProtocols: List<String> = superProtocols()
    val categoryName: String? = null
    val generics: List<ObjCGenericTypeDeclaration> = emptyList()
    val superClassGenerics: List<ObjCNonNullReferenceType> = emptyList()
    val objectMembers = getDefaultMembers()

    getAllMembers().flatMap { it.translateToObjCExportStubs() }.forEach {
        objectMembers.add(it)
    }

    return ObjCInterfaceImpl(
        name.objCName,
        comment,
        origin,
        attributes,
        superProtocols,
        objectMembers,
        categoryName,
        generics,
        superName.objCName,
        superClassGenerics
    )
}

context(KtAnalysisSession, KtObjCExportSession)
private fun KtClassOrObjectSymbol.getDefaultMembers(): MutableList<ObjCExportStub> {

    val result = mutableListOf<ObjCExportStub>()
    val allocWithZoneParameter = ObjCParameter("zone", null, ObjCRawType("struct _NSZone *"), null)

    result.add(
        ObjCMethod(null, null, false, ObjCInstanceType, listOf("alloc"), emptyList(), listOf("unavailable"))
    )

    result.add(
        ObjCMethod(null, null, false, ObjCInstanceType, listOf("allocWithZone:"), listOf(allocWithZoneParameter), listOf("unavailable"))
    )

    result.add(
        ObjCMethod(
            null,
            null,
            false,
            ObjCInstanceType,
            listOf(getObjectInstanceSelector(this)),
            emptyList(),
            listOf(swiftNameAttribute("init()"))
        )
    )

    result.add(
        ObjCProperty(
            name = ObjCPropertyNames.objectPropertyName,
            comment = null,
            type = toPropertyType(),
            propertyAttributes = listOf("class", "readonly"),
            getterName = getObjectPropertySelector(this),
            declarationAttributes = listOf(swiftNameAttribute(ObjCPropertyNames.objectPropertyName)),
            origin = null
        )
    )
    return result
}

/**
 * TODO: Temp implementation
 * Use translateToObjCReferenceType() to make type
 * See also: [org.jetbrains.kotlin.backend.konan.objcexport.ObjCExportTranslatorImpl.mapReferenceType]
 */
private fun KtClassOrObjectSymbol.toPropertyType() = ObjCClassType(
    this.classIdIfNonLocal!!.shortClassName.asString(),
    emptyList()
)

/**
 * [org.jetbrains.kotlin.backend.konan.objcexport.ObjCExportNamerImpl.getObjectInstanceSelector]
 */
context(KtAnalysisSession, KtObjCExportSession)
private fun getObjectInstanceSelector(objectSymbol: KtClassOrObjectSymbol): String {
    return objectSymbol.getObjCClassOrProtocolName().objCName.lowercase()
}

/**
 * [org.jetbrains.kotlin.backend.konan.objcexport.ObjCExportNamerImpl.getObjectPropertySelector]
 */
context(KtAnalysisSession, KtObjCExportSession)
private fun getObjectPropertySelector(descriptor: KtClassOrObjectSymbol): String {
    val collides = ObjCPropertyNames.objectPropertyName == getObjectInstanceSelector(descriptor)
    return ObjCPropertyNames.objectPropertyName + (if (collides) "_" else "")
}

