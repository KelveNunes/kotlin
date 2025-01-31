/*
 * Copyright 2010-2024 JetBrains s.r.o. and Kotlin Programming Language contributors.
 * Use of this source code is governed by the Apache 2.0 license that can be found in the license/LICENSE.txt file.
 */

package org.jetbrains.kotlin.analysis.low.level.api.fir

import junit.framework.TestCase
import org.jetbrains.kotlin.analysis.low.level.api.fir.lazy.resolve.FirLazyBodiesCalculator
import org.jetbrains.kotlin.analysis.low.level.api.fir.test.configurators.AnalysisApiFirOutOfContentRootTestConfigurator
import org.jetbrains.kotlin.analysis.low.level.api.fir.test.configurators.AnalysisApiFirSourceTestConfigurator
import org.jetbrains.kotlin.analysis.test.framework.base.AbstractAnalysisApiBasedTest
import org.jetbrains.kotlin.fir.FirElement
import org.jetbrains.kotlin.fir.builder.BodyBuildingMode
import org.jetbrains.kotlin.fir.builder.PsiRawFirBuilder
import org.jetbrains.kotlin.fir.expressions.FirLazyBlock
import org.jetbrains.kotlin.fir.expressions.FirLazyExpression
import org.jetbrains.kotlin.fir.renderer.FirRenderer
import org.jetbrains.kotlin.fir.scopes.kotlinScopeProvider
import org.jetbrains.kotlin.fir.visitors.FirVisitorVoid
import org.jetbrains.kotlin.psi.KtFile
import org.jetbrains.kotlin.test.model.TestModule
import org.jetbrains.kotlin.test.services.TestServices

abstract class AbstractFirLazyBodiesCalculatorTest : AbstractAnalysisApiBasedTest() {
    private val lazyChecker = object : FirVisitorVoid() {
        override fun visitElement(element: FirElement) {
            TestCase.assertFalse("${FirLazyBlock::class.qualifiedName} should not present in the tree", element is FirLazyBlock)
            TestCase.assertFalse("${FirLazyExpression::class.qualifiedName} should not present in the tree", element is FirLazyExpression)
            element.acceptChildren(this)
        }
    }

    override fun doTestByMainFile(mainFile: KtFile, mainModule: TestModule, testServices: TestServices) {
        resolveWithClearCaches(mainFile) { firResolveSession ->
            val session = firResolveSession.useSiteFirSession
            val provider = session.kotlinScopeProvider

            val laziedFirFile = PsiRawFirBuilder(
                session,
                provider,
                bodyBuildingMode = BodyBuildingMode.LAZY_BODIES
            ).buildFirFile(mainFile)

            FirLazyBodiesCalculator.calculateAllLazyExpressionsInFile(laziedFirFile)
            laziedFirFile.accept(lazyChecker)

            val fullFirFile = PsiRawFirBuilder(
                session,
                provider,
                bodyBuildingMode = BodyBuildingMode.NORMAL
            ).buildFirFile(mainFile)

            val laziedFirFileDump = FirRenderer().renderElementAsString(laziedFirFile)
            val fullFirFileDump = FirRenderer().renderElementAsString(fullFirFile)

            TestCase.assertEquals(laziedFirFileDump, fullFirFileDump)
        }
    }
}

abstract class AbstractFirSourceLazyBodiesCalculatorTest : AbstractFirLazyBodiesCalculatorTest() {
    override val configurator = AnalysisApiFirSourceTestConfigurator(analyseInDependentSession = false)
}

abstract class AbstractFirOutOfContentRootLazyBodiesCalculatorTest : AbstractFirLazyBodiesCalculatorTest() {
    override val configurator = AnalysisApiFirOutOfContentRootTestConfigurator
}